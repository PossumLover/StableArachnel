# Patch QmlMaterial: on WIN32 it forces STATIC, which breaks MinGW plugin linking.
# Drop OPTIMIZED shaders (spirv-opt / Vulkan SDK) and PkgConfig REQUIRED
# (QmlMaterial never calls pkg_check_modules; local Qt kits have no pkg-config).
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED SRC)
    message(FATAL_ERROR "SRC not set")
endif()
file(READ "${SRC}/CMakeLists.txt" _content)
string(REPLACE
    "    set(QML_MATERIAL_BUILD_TYPE STATIC)"
    "    set(QML_MATERIAL_BUILD_TYPE SHARED)"
    _content "${_content}")
string(REPLACE
    "set(SHADER_OPT BATCHABLE OPTIMIZED)"
    "set(SHADER_OPT BATCHABLE)"
    _content "${_content}")
if(WIN32)
    string(REGEX REPLACE "find_package\\(PkgConfig REQUIRED\\)[^\n]*\n" "" _content "${_content}")
endif()
file(WRITE "${SRC}/CMakeLists.txt" "${_content}")

# ---------------------------------------------------------------------------
# "Meadow": a hand-picked palette instead of one generated from a single seed.
# Every generated scheme derives secondary/tertiary/neutral from ONE colour, so
# it cannot give a sage primary, a dusty-rose tertiary and warm cream surfaces at
# once. material-color-utilities' DynamicScheme accepts five palettes directly;
# this adds PaletteType::PaletteMeadow that builds one from colours sampled off
# the reference paintings. Idempotent: re-running configure changes nothing.
# ---------------------------------------------------------------------------
set(_enum "${SRC}/include/qml_material/core/enum.hpp")
file(READ "${_enum}" _e)
if(NOT _e MATCHES "PaletteMeadow")
    string(REPLACE "        PaletteFruitSalad,\n" "        PaletteFruitSalad,\n        PaletteMeadow,\n" _e "${_e}")
    file(WRITE "${_enum}" "${_e}")
endif()

set(_helper "${SRC}/src/core/helper.cpp")
file(READ "${_helper}" _h)
if(NOT _h MATCHES "PaletteMeadow")
    string(REPLACE
"auto genScheme(qml_material::Enum::PaletteType t, Hct hct, bool is_dark, double ct_level) {"
"md::DynamicScheme meadowScheme(Hct source, bool is_dark, double ct_level) {
    // Keep each colour's hue; lift chroma just enough that buttons and chips read
    // as coloured rather than grey. Neutrals stay warm and nearly unsaturated.
    auto palette = [](md::Argb argb, double min_chroma) {
        const Hct h(argb);
        return md::TonalPalette(h.get_hue(), std::max(h.get_chroma(), min_chroma));
    };
    const Hct cream(0xFFF0EBE3);
    return md::DynamicScheme(source, md::Variant::kTonalSpot, ct_level, is_dark,
                             palette(0xFF435A4D, 26.0),        // forest sage
                             palette(0xFF5F6A42, 18.0),        // olive
                             palette(0xFFBA9088, 28.0),        // dusty rose
                             md::TonalPalette(cream.get_hue(), 5.0),
                             md::TonalPalette(cream.get_hue(), 9.0));
}

auto genScheme(qml_material::Enum::PaletteType t, Hct hct, bool is_dark, double ct_level) {"
        _h "${_h}")
    string(REPLACE
"    case ET::PaletteVibrant: {"
"    case ET::PaletteMeadow: {
        convert_from(out, meadowScheme(hct, is_dark, ct_level));
        break;
    }
    case ET::PaletteVibrant: {"
        _h "${_h}")
    file(WRITE "${_helper}" "${_h}")
endif()
