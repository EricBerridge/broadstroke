broadstroke_add_shaders(
  TARGET broadstroke_shaders
  SOURCES
    hello_double.comp
    support_visualize.comp
    pencil_stroke.comp
    watercolor_stroke.comp
    wetness_diffuse.comp
    pastel_stroke.comp
    pastel_smear.comp
    substrate_to_rgba.comp
    apply_graphite.comp
    apply_pigment.comp
)

broadstroke_add_shader_variants(
  TARGET broadstroke_shader_variants
  LOCAL_SIZES_2D 8x8 16x16
  SOURCES
    pencil_stroke.comp
    watercolor_stroke.comp
    wetness_diffuse.comp
    pastel_stroke.comp
    pastel_smear.comp
    substrate_to_rgba.comp
    apply_graphite.comp
    apply_pigment.comp
)
