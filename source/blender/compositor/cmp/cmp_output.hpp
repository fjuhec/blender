#ifndef CMP_OUTPUT_HPP
#define CMP_OUTPUT_HPP

#include "cmp_node.hpp"

namespace Compositor {
  struct Output {
    bNodeTree * node_tree;
    Node* node;

    int width;
    int height;

    float *buffer;


    // Image Editor
    Image *m_image;
    ImageUser *m_imageUser;
    const RenderData *m_rd;
    const char *m_viewName;
    ImBuf *m_ibuf;

    // COLOR MANAGEMENT
    const ColorManagedViewSettings *m_viewSettings;
    const ColorManagedDisplaySettings *m_displaySettings;

    Output(bNodeTree *node_tree, Node* node, RenderData *rd, const char* view_name, const ColorManagedViewSettings *viewSettings, const ColorManagedDisplaySettings *displaySettings);
    void update_subimage(int x_min, int y_min, int x_max, int y_max);

  private:
    void init_image();



  };
}
#endif
