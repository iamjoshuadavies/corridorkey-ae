/*
    CorridorKeyAE_UI.cpp
    Custom UI drawing — logo, branding text, About link.
    Uses Drawbot API for rendering in the effect controls panel.
*/

#include "CorridorKeyAE_UI.h"
#include "CorridorKeyAE_Params.h"
#include "CorridorKeyAE_Version.h"

#if AE_SDK_AVAILABLE

#include "AEFX_SuiteHelper.h"
#include "AE_EffectSuites.h"
#include "adobesdk/DrawbotSuite.h"

// Embedded logo pixel data
#include "CorridorKeyAE_Logo.h"

namespace corridorkey {

// =============================================================================
// Helper: Convert ASCII to UTF16 for Drawbot
// =============================================================================
static void AsciiToUTF16(const char* ascii, DRAWBOT_UTF16Char* utf16, int maxLen) {
    int i = 0;
    while (ascii[i] && i < maxLen - 1) {
        utf16[i] = static_cast<DRAWBOT_UTF16Char>(ascii[i]);
        i++;
    }
    utf16[i] = 0;
}

// =============================================================================
// Draw event — render the branded header
// =============================================================================
static PF_Err DrawEvent(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_EventExtra*  event_extra)
{
    PF_Err err = PF_Err_NONE;

    // Only draw in the effect control area
    if (event_extra->effect_win.area != PF_EA_CONTROL) {
        return PF_Err_NONE;
    }

    // Get the Drawbot suites
    DRAWBOT_Suites suites;
    ERR(AEFX_AcquireDrawbotSuites(in_data, out_data, &suites));
    if (err) return err;

    // Get drawing reference from context
    PF_EffectCustomUISuite1* customUISuite = nullptr;
    ERR(AEFX_AcquireSuite(in_data, out_data,
        kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1,
        nullptr, reinterpret_cast<void**>(&customUISuite)));

    if (!customUISuite) {
        AEFX_ReleaseDrawbotSuites(in_data, out_data);
        return PF_Err_NONE;
    }

    DRAWBOT_DrawRef draw_ref = nullptr;
    ERR(customUISuite->PF_GetDrawingReference(event_extra->contextH, &draw_ref));
    if (err || !draw_ref) {
        AEFX_ReleaseSuite(in_data, out_data,
            kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, nullptr);
        AEFX_ReleaseDrawbotSuites(in_data, out_data);
        return err;
    }

    // Get supplier and surface
    DRAWBOT_SupplierRef supplier = nullptr;
    DRAWBOT_SurfaceRef surface = nullptr;
    suites.drawbot_suiteP->GetSupplier(draw_ref, &supplier);
    suites.drawbot_suiteP->GetSurface(draw_ref, &surface);

    // Control frame coordinates
    float cx = static_cast<float>(event_extra->effect_win.current_frame.left);
    float cy = static_cast<float>(event_extra->effect_win.current_frame.top);

    // --- Draw logo image ---
    DRAWBOT_ImageRef logo_ref = nullptr;
    suites.supplier_suiteP->NewImageFromBuffer(
        supplier,
        CK_LOGO_WIDTH, CK_LOGO_HEIGHT, CK_LOGO_ROWBYTES,
        kDRAWBOT_PixelLayout_32BGRA_Premul,
        CK_LOGO_BGRA,
        &logo_ref
    );

    if (logo_ref) {
        float logo_x = cx + 10.0f;
        float logo_y = cy + 10.0f;
        DRAWBOT_PointF32 logo_origin = {logo_x, logo_y};
        suites.surface_suiteP->DrawImage(surface, logo_ref, &logo_origin, 1.0f);
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(logo_ref));
    }

    // --- Draw "CorridorKey" title ---
    float text_x = cx + 10.0f + 38.0f + 10.0f;  // After logo + padding
    float title_y = cy + 16.0f;  // More room for ascenders

    DRAWBOT_FontRef title_font = nullptr;
    float default_size = 0;
    suites.supplier_suiteP->GetDefaultFontSize(supplier, &default_size);
    suites.supplier_suiteP->NewDefaultFont(supplier, default_size * 1.6f, &title_font);

    DRAWBOT_ColorRGBA title_color = {0.95f, 0.95f, 0.95f, 1.0f};
    DRAWBOT_BrushRef title_brush = nullptr;
    suites.supplier_suiteP->NewBrush(supplier, &title_color, &title_brush);

    DRAWBOT_UTF16Char title_text[64];
    AsciiToUTF16("CorridorKey", title_text, 64);
    DRAWBOT_PointF32 title_origin = {text_x, title_y};
    suites.surface_suiteP->DrawString(
        surface, title_brush, title_font,
        title_text, &title_origin,
        kDRAWBOT_TextAlignment_Left,
        kDRAWBOT_TextTruncation_None, 0.0f
    );

    if (title_brush)
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(title_brush));
    if (title_font)
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(title_font));

    // --- Draw tagline ---
    float tagline_y = title_y + default_size * 1.8f;

    DRAWBOT_FontRef tag_font = nullptr;
    suites.supplier_suiteP->NewDefaultFont(supplier, default_size * 0.85f, &tag_font);

    DRAWBOT_ColorRGBA tag_color = {0.6f, 0.6f, 0.6f, 1.0f};
    DRAWBOT_BrushRef tag_brush = nullptr;
    suites.supplier_suiteP->NewBrush(supplier, &tag_color, &tag_brush);

    DRAWBOT_UTF16Char tag_text[128];
    AsciiToUTF16("Advanced Green Screen Keying", tag_text, 128);
    DRAWBOT_PointF32 tag_origin = {text_x, tagline_y};
    suites.surface_suiteP->DrawString(
        surface, tag_brush, tag_font,
        tag_text, &tag_origin,
        kDRAWBOT_TextAlignment_Left,
        kDRAWBOT_TextTruncation_None, 0.0f
    );

    // --- Draw "About" link ---
    float about_y = tagline_y + default_size * 1.3f;

    DRAWBOT_ColorRGBA about_color = {0.4f, 0.6f, 1.0f, 1.0f}; // Blue link color
    DRAWBOT_BrushRef about_brush = nullptr;
    suites.supplier_suiteP->NewBrush(supplier, &about_color, &about_brush);

    DRAWBOT_UTF16Char about_text[32];
    AsciiToUTF16("About", about_text, 32);
    DRAWBOT_PointF32 about_origin = {text_x, about_y};
    suites.surface_suiteP->DrawString(
        surface, about_brush, tag_font,
        about_text, &about_origin,
        kDRAWBOT_TextAlignment_Left,
        kDRAWBOT_TextTruncation_None, 0.0f
    );

    // Draw underline for "About" — tight to the text
    DRAWBOT_PathRef underline_path = nullptr;
    suites.supplier_suiteP->NewPath(supplier, &underline_path);
    if (underline_path) {
        float underline_y_pos = about_y + default_size * 0.05f;
        DRAWBOT_PointF32 p1 = {text_x, underline_y_pos};
        DRAWBOT_PointF32 p2 = {text_x + 28.0f, underline_y_pos};
        suites.path_suiteP->MoveTo(underline_path, p1.x, p1.y);
        suites.path_suiteP->LineTo(underline_path, p2.x, p2.y);

        DRAWBOT_PenRef pen = nullptr;
        suites.supplier_suiteP->NewPen(supplier, &about_color, 1.0f, &pen);
        if (pen) {
            suites.surface_suiteP->StrokePath(surface, pen, underline_path);
            suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(pen));
        }
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(underline_path));
    }

    // Cleanup
    if (about_brush)
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(about_brush));
    if (tag_brush)
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(tag_brush));
    if (tag_font)
        suites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(tag_font));

    // Release suites
    AEFX_ReleaseSuite(in_data, out_data,
        kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, nullptr);
    AEFX_ReleaseDrawbotSuites(in_data, out_data);

    event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
    return PF_Err_NONE;
}

// =============================================================================
// Click event — handle "About" link click
// =============================================================================
static PF_Err ClickEvent(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_EventExtra*  event_extra)
{
    if (event_extra->effect_win.area != PF_EA_CONTROL) {
        return PF_Err_NONE;
    }

    // Match the draw layout coordinates
    float cx = static_cast<float>(event_extra->effect_win.current_frame.left);
    float cy = static_cast<float>(event_extra->effect_win.current_frame.top);
    float mx = static_cast<float>(event_extra->u.do_click.screen_point.h);
    float my = static_cast<float>(event_extra->u.do_click.screen_point.v);

    // "About" link area: text_x = cx + 58, positioned below tagline
    float text_x = cx + 58.0f;
    float about_top = cy + 42.0f;   // Approximate y of About text
    float about_bottom = cy + 58.0f;

    if (mx >= text_x && mx <= text_x + 40.0f &&
        my >= about_top && my <= about_bottom) {
        PF_SPRINTF(out_data->return_msg,
            "CorridorKey v%s\n\n"
            "Advanced green-screen keying for After Effects.\n"
            "Physically accurate foreground/background unmixing\n"
            "powered by MLX on Apple Silicon.\n\n"
            "Based on CorridorKey by Niko Pueringer / Corridor Digital.",
            CK_VERSION_STRING
        );
        out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        event_extra->evt_out_flags = PF_EO_HANDLED_EVENT;
    }

    return PF_Err_NONE;
}

// =============================================================================
// Event dispatcher
// =============================================================================
A_Err HandleEvent(
    PF_InData*      in_data,
    PF_OutData*     out_data,
    PF_ParamDef*    params[],
    PF_LayerDef*    output,
    PF_EventExtra*  event_extra)
{
    PF_Err err = PF_Err_NONE;

    switch (event_extra->e_type) {
        case PF_Event_DRAW:
            err = DrawEvent(in_data, out_data, event_extra);
            break;

        case PF_Event_DO_CLICK:
            err = ClickEvent(in_data, out_data, event_extra);
            break;

        case PF_Event_ADJUST_CURSOR:
            // Could change cursor to hand over "About" link
            break;

        default:
            break;
    }

    return err;
}

// =============================================================================
// Register custom UI
// =============================================================================
A_Err RegisterCustomUI(PF_InData* in_data)
{
    PF_CustomUIInfo ci;
    AEFX_CLR_STRUCT(ci);
    ci.events = PF_CustomEFlag_EFFECT;  // We want effect window events
    ci.comp_ui_width = 0;
    ci.comp_ui_height = 0;
    ci.layer_ui_width = 0;
    ci.layer_ui_height = 0;
    ci.preview_ui_width = 0;
    ci.preview_ui_height = 0;

    return (*in_data->inter.register_ui)(in_data->effect_ref, &ci);
}

} // namespace corridorkey

#endif // AE_SDK_AVAILABLE
