/*  material.h: the materials named at run time, beside the fixed ones in
 *  generated/materials.h.  The outline view names its own. */
#ifndef ARC_MATERIAL_H
#define ARC_MATERIAL_H

/*  A material by its name, a color and a roughness.  Answers the number
 *  it is known by, MAT_RUNTIME_BASE and up, or -1 where there is no room.
 *  The same name twice answers the same number and rewrites its
 *  parameters. */
float material_define(const char *name, const float rgb[3], float rough);
/*  Every material named so far, as a packed run of color and roughness:
 *  the count, and the run in out. */
int material_list(const float **out);
/*  Say that a material draws a feature `width` tiles across at its
 *  thinnest.  Where that spans less than a pixel the shader leaves the
 *  material out, so a detail too fine to resolve does not flicker.  0,
 *  the default, is a material of no fine feature. */
void material_fine(float mat, float width);
/*  Each material's thinnest feature, in tiles, in the order of
 *  material_list. */
const float *material_fine_list(void);

#endif /* ARC_MATERIAL_H */
