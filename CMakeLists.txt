cmake_minimum_required(VERSION 3.15)
project(CorridorKeyAE_Root VERSION 0.1.0)

# Root CMake — orchestrates the plugin build.
# The Python runtime is managed separately via pip/pyproject.toml.

add_subdirectory(plugin)
