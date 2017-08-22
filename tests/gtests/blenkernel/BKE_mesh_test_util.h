/* Apache License, Version 2.0 */

#include <iosfwd>

struct Mesh;

struct Mesh* BKE_mesh_test_from_data(
        const float (*verts)[3], int numverts,
        const int (*edges)[2], int numedges,
        const int *loops, const int *face_lengths, int numfaces);

void BKE_mesh_test_dump_verts(struct Mesh *me, std::ostream &str);
void BKE_mesh_test_dump_edges(struct Mesh *me, std::ostream &str);
void BKE_mesh_test_dump_faces(struct Mesh *me, std::ostream &str);
void BKE_mesh_test_dump_mesh(struct Mesh *me, const char *name, std::ostream &str);
