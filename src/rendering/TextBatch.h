#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string_view>
#include <vector>

struct TextVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
};

static_assert(sizeof(TextVertex) == 32);

// Positions are framebuffer pixels from the top left; scale is an integer pixel size.
// Printable ASCII is supported. Other UTF-8 characters become one '?' per code point.
// Measurement returns advance boxes (including trailing spaces), excluding the shadow.
class TextBatch {
public:
    static glm::ivec2 MeasureText(std::string_view text, int scale = 2);

    void DrawText(std::string_view text, glm::vec2 position, int scale = 2, glm::vec4 color = glm::vec4(1),
                  bool shadow = true);

    void Clear() { m_Vertices.clear(); }

    const std::vector<TextVertex>& Vertices() const { return m_Vertices; }

private:
    std::vector<TextVertex> m_Vertices;
};
