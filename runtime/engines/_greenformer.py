"""
Vendored PyTorch implementation of CorridorKey's GreenFormer.

This is a clean reimplementation of the model architecture, adapted from
nikopueringer/corridorkey-mlx's reference dump script
(scripts/dump_pytorch_reference.py). The architecture mirrors the original
nikopueringer/CorridorKey GreenFormer:

  Hiera (hiera_base_plus_224) encoder, patched to 4 input channels
    -> dual SegFormer-style decoder heads (alpha + foreground)
    -> CNN refiner (stem + 4 dilated residual blocks + 1x1 projection)

The encoder's positional embedding is fixed-size at construction time, so
to support multiple inference resolutions (the Quality dropdown) we
bicubic-interpolate the checkpoint's pos_embed when the requested
img_size differs from the checkpoint's native size. Same trick the
upstream reference dump script uses.

Upstream:
  https://github.com/nikopueringer/corridorkey-mlx
  https://github.com/nikopueringer/CorridorKey  (CC BY-NC-SA 4.0)
"""

from __future__ import annotations

import math
from collections import OrderedDict
from typing import Any, cast

import timm
import torch
import torch.nn as nn  # noqa: N812  (PyTorch convention)
import torch.nn.functional as F  # noqa: N812  (PyTorch convention)

# Architecture constants (must match the trained checkpoint)
HIERA_MODEL_NAME = "hiera_base_plus_224.mae_in1k_ft_in1k"
BACKBONE_CHANNELS = [112, 224, 448, 896]
EMBED_DIM = 256
INPUT_CHANNELS = 4   # RGB + alpha hint
REFINER_CHANNELS = 64
REFINER_GROUPS = 8
REFINER_SCALE = 10.0
DROPOUT_RATE = 0.1


class _MLP(nn.Module):
    def __init__(self, input_dim: int, embed_dim: int) -> None:
        super().__init__()
        self.proj = nn.Linear(input_dim, embed_dim)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # torch's public stubs type nn.Linear.__call__ as returning Any; cast
        # to keep the rest of the module strictly typed.
        return cast(torch.Tensor, self.proj(x))


class _DecoderHead(nn.Module):
    """SegFormer-style multiscale feature fusion head."""

    def __init__(self, in_channels: list[int], embed_dim: int, output_dim: int) -> None:
        super().__init__()
        self.linear_c1 = _MLP(in_channels[0], embed_dim)
        self.linear_c2 = _MLP(in_channels[1], embed_dim)
        self.linear_c3 = _MLP(in_channels[2], embed_dim)
        self.linear_c4 = _MLP(in_channels[3], embed_dim)

        fused_channels = embed_dim * len(in_channels)
        self.linear_fuse = nn.Conv2d(fused_channels, embed_dim, kernel_size=1, bias=False)
        self.bn = nn.BatchNorm2d(embed_dim)
        self.classifier = nn.Conv2d(embed_dim, output_dim, kernel_size=1)

    def forward(self, features: list[torch.Tensor]) -> torch.Tensor:
        c1, c2, c3, c4 = features
        target_size = c1.shape[2:]  # H/4, W/4

        projected = []
        for feat, linear in zip(
            [c1, c2, c3, c4],
            [self.linear_c1, self.linear_c2, self.linear_c3, self.linear_c4],
        ):
            b, c, h, w = feat.shape
            x = feat.flatten(2).transpose(1, 2)        # [B, H*W, C]
            x = linear(x)                              # [B, H*W, embed_dim]
            x = x.transpose(1, 2).reshape(b, -1, h, w) # [B, embed_dim, H, W]
            x = F.interpolate(x, size=target_size, mode="bilinear", align_corners=False)
            projected.append(x)

        # Concatenation order matches the upstream PyTorch reference.
        fused = torch.cat(projected[::-1], dim=1)
        fused = self.linear_fuse(fused)
        fused = self.bn(fused)
        fused = F.relu(fused)
        fused = F.dropout(fused, p=DROPOUT_RATE, training=self.training)
        return cast(torch.Tensor, self.classifier(fused))


class _RefinerBlock(nn.Module):
    """Dilated residual block with GroupNorm."""

    def __init__(self, channels: int, dilation: int) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(
            channels, channels, kernel_size=3, padding=dilation, dilation=dilation
        )
        self.gn1 = nn.GroupNorm(REFINER_GROUPS, channels)
        self.conv2 = nn.Conv2d(
            channels, channels, kernel_size=3, padding=dilation, dilation=dilation
        )
        self.gn2 = nn.GroupNorm(REFINER_GROUPS, channels)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        residual = x
        out = F.relu(self.gn1(self.conv1(x)))
        out = self.gn2(self.conv2(out))
        return F.relu(out + residual)


class _CNNRefinerModule(nn.Module):
    """CNN refiner: stem + 4 dilated residual blocks + 1x1 projection."""

    def __init__(self) -> None:
        super().__init__()
        # 7 input channels: RGB (3) + coarse_pred (4: alpha + fg)
        self.stem = nn.Sequential(
            nn.Conv2d(7, REFINER_CHANNELS, kernel_size=3, padding=1),
            nn.GroupNorm(REFINER_GROUPS, REFINER_CHANNELS),
            nn.ReLU(),
        )
        self.res1 = _RefinerBlock(REFINER_CHANNELS, dilation=1)
        self.res2 = _RefinerBlock(REFINER_CHANNELS, dilation=2)
        self.res3 = _RefinerBlock(REFINER_CHANNELS, dilation=4)
        self.res4 = _RefinerBlock(REFINER_CHANNELS, dilation=8)
        # 4 output channels: delta for alpha (1) + delta for fg (3)
        self.final = nn.Conv2d(REFINER_CHANNELS, 4, kernel_size=1)

    def forward(self, rgb: torch.Tensor, coarse_pred: torch.Tensor) -> torch.Tensor:
        x = torch.cat([rgb, coarse_pred], dim=1)
        x = self.stem(x)
        x = self.res1(x)
        x = self.res2(x)
        x = self.res3(x)
        x = self.res4(x)
        return cast(torch.Tensor, self.final(x) * REFINER_SCALE)


class GreenFormer(nn.Module):
    """Top-level model: Hiera encoder + dual decoders + optional refiner.

    Args:
        img_size: Input spatial resolution (square). The encoder's pos_embed
            is created at this size; load_checkpoint() will interpolate from
            whatever size the .pth was trained at.
    """

    def __init__(self, img_size: int = 2048) -> None:
        super().__init__()
        self.img_size = img_size

        self.encoder = timm.create_model(
            HIERA_MODEL_NAME,
            pretrained=False,
            features_only=True,
            img_size=img_size,
        )
        self._patch_first_conv()

        self.alpha_decoder = _DecoderHead(BACKBONE_CHANNELS, EMBED_DIM, output_dim=1)
        self.fg_decoder    = _DecoderHead(BACKBONE_CHANNELS, EMBED_DIM, output_dim=3)
        self.refiner       = _CNNRefinerModule()

    def _patch_first_conv(self) -> None:
        """Replace 3-channel patch embed conv with 4-channel version."""
        old_conv = self.encoder.model.patch_embed.proj
        new_conv = nn.Conv2d(
            INPUT_CHANNELS,
            old_conv.out_channels,
            kernel_size=old_conv.kernel_size,
            stride=old_conv.stride,
            padding=old_conv.padding,
            bias=old_conv.bias is not None,
        )
        nn.init.zeros_(new_conv.weight)
        if old_conv.bias is not None:
            new_conv.bias = old_conv.bias
        new_conv.weight.data[:, :3] = old_conv.weight.data
        self.encoder.model.patch_embed.proj = new_conv

    def forward(
        self,
        x: torch.Tensor,
        refiner_scale: torch.Tensor | None = None,
        skip_refiner: bool = False,
    ) -> dict[str, torch.Tensor]:
        input_size = x.shape[2:]

        features = self.encoder(x)

        alpha_logits = self.alpha_decoder(features)
        fg_logits    = self.fg_decoder(features)

        alpha_logits_up = F.interpolate(
            alpha_logits, size=input_size, mode="bilinear", align_corners=False
        )
        fg_logits_up = F.interpolate(
            fg_logits, size=input_size, mode="bilinear", align_corners=False
        )

        if skip_refiner:
            alpha_final = torch.sigmoid(alpha_logits_up)
            fg_final    = torch.sigmoid(fg_logits_up)
        else:
            alpha_coarse = torch.sigmoid(alpha_logits_up)
            fg_coarse    = torch.sigmoid(fg_logits_up)
            coarse_pred  = torch.cat([alpha_coarse, fg_coarse], dim=1)

            rgb = x[:, :3]
            delta_logits = self.refiner(rgb, coarse_pred)
            delta_alpha = delta_logits[:, 0:1]
            delta_fg    = delta_logits[:, 1:4]

            # refiner_scale only affects the alpha edge correction. The fg
            # delta always gets the full correction (matches original GreenFormer
            # behaviour — prevents Hiera attention-window block artifacts).
            if refiner_scale is not None:
                if torch.is_tensor(refiner_scale):
                    refiner_scale = refiner_scale.to(
                        device=delta_alpha.device, dtype=delta_alpha.dtype
                    )
                delta_alpha = delta_alpha * refiner_scale

            alpha_final = torch.sigmoid(alpha_logits_up + delta_alpha)
            fg_final    = torch.sigmoid(fg_logits_up + delta_fg)

        return {"alpha": alpha_final, "fg": fg_final}


# ---------------------------------------------------------------------------
# Pos-embed interpolation — enables loading a checkpoint trained at one
# resolution into a model built at a different resolution.
# ---------------------------------------------------------------------------

def interpolate_pos_embed(
    ckpt_embed: torch.Tensor,
    target_n: int,
    embed_dim: int,
) -> torch.Tensor:
    """Bicubic-interpolate Hiera pos_embed from checkpoint size to target size.

    Args:
        ckpt_embed: (1, ckpt_n, embed_dim) tensor from the checkpoint.
        target_n:   Number of spatial tokens at the target img_size
                    (must be a perfect square).
        embed_dim:  Channel dim of the pos_embed.

    Returns:
        (1, target_n, embed_dim) tensor.
    """
    ckpt_n = ckpt_embed.shape[1]
    if ckpt_n == target_n:
        return ckpt_embed

    ckpt_side   = int(math.sqrt(ckpt_n))
    target_side = int(math.sqrt(target_n))
    assert ckpt_side * ckpt_side == ckpt_n, f"pos_embed not square: {ckpt_n}"
    assert target_side * target_side == target_n, f"pos_embed not square: {target_n}"

    embed = (
        ckpt_embed.reshape(1, ckpt_side, ckpt_side, embed_dim).permute(0, 3, 1, 2)
    )
    embed = F.interpolate(
        embed, size=(target_side, target_side), mode="bicubic", align_corners=False
    )
    return cast(torch.Tensor, embed.permute(0, 2, 3, 1).reshape(1, target_n, embed_dim))


def load_state_dict_into(
    model: GreenFormer, state_dict: dict[str, Any],
) -> tuple[list[str], list[str]]:
    """Load a (cleaned) PyTorch state_dict into the model, interpolating
    pos_embed if the checkpoint and model were built at different img_sizes.

    The state_dict should already have the `_orig_mod.` torch.compile prefix
    stripped (caller's responsibility — see _weights_loader.py).

    Returns (missing_keys, unexpected_keys) like load_state_dict.
    """
    pos_key = "encoder.model.pos_embed"
    if pos_key in state_dict:
        model_embed = model.state_dict()[pos_key]
        if state_dict[pos_key].shape != model_embed.shape:
            state_dict = OrderedDict(state_dict)  # don't mutate caller's dict
            state_dict[pos_key] = interpolate_pos_embed(
                state_dict[pos_key],
                target_n=model_embed.shape[1],
                embed_dim=BACKBONE_CHANNELS[0],
            )

    missing, unexpected = model.load_state_dict(state_dict, strict=False)
    return list(missing), list(unexpected)
