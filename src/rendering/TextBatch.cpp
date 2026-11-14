#include "rendering/TextBatch.h"
#include "rendering/BitmapFont.h"

#include <algorithm>
#include <cmath>

namespace {
// Layout and measurement deliberately share this traversal.
template<class Emit>
glm::ivec2 Layout(std::string_view text, int scale, Emit emit) {
    if (text.empty() || scale <= 0) {
        return {};
    }

    int x = 0, y = 0, width = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);

		// Handle control characters and whitespace.
        switch (ch) {
            case '\r': {
                continue;
            }
            case '\n': {
                width = std::max(width, x);
                x = 0;
                y += BitmapFont::LINE_HEIGHT;

                continue;
            }
			case '\t': {
				x = (x / (4 * BitmapFont::ADVANCE) + 1) * 4 * BitmapFont::ADVANCE;
				continue;
			}

        }

        if (ch < BitmapFont::ASCII_START || ch > BitmapFont::ASCII_END) {
            // Consume continuation bytes of a non-ASCII UTF-8 sequence.
            if (ch >= 0xC0) {
                while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
                    ++i;
                }
            }

            ch = '?';
        }

        if (ch != ' ') {
            emit(ch - 32, x * scale, y * scale);
        }

        x += BitmapFont::ADVANCE;
    }

    // Advance-box measurement includes trailing spaces, useful for consistent alignment.
    return {std::max(width, x) * scale, (y + BitmapFont::LINE_HEIGHT) * scale};
}
}

glm::ivec2 TextBatch::MeasureText(std::string_view text, int scale) {
    return Layout(text, scale, [](int, int, int) {});
}

void TextBatch::DrawText(std::string_view text, glm::vec2 position, int scale, glm::vec4 color, bool shadow) {
    if (scale <= 0 || !std::isfinite(position.x) || !std::isfinite(position.y)) {
        return;
    }

    position = {std::round(position.x), std::round(position.y)};

    auto append = [&](glm::vec2 origin, glm::vec4 tint) {
        Layout(text, scale, [&](int glyph, int x, int y) {
            const glm::vec2 p = origin + glm::vec2(x, y);
            const glm::vec2 size(BitmapFont::GLYPH_WIDTH * scale, BitmapFont::GLYPH_HEIGHT * scale);

            const glm::vec2 uv(float(glyph % 16 * 8) / BitmapFont::WIDTH,
                float(glyph / 16 * 8) / BitmapFont::HEIGHT);
            const glm::vec2 uvSize(float(BitmapFont::GLYPH_WIDTH) / BitmapFont::WIDTH,
                float(BitmapFont::GLYPH_HEIGHT) / BitmapFont::HEIGHT);

            constexpr glm::vec2 corners[] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
            for (auto corner : corners) {
                m_Vertices.push_back({p + corner * size, uv + corner * uvSize, tint});
            }
        });
    };

    if (shadow) {
        append(position + glm::vec2(float(scale)), {0, 0, 0, color.a * 0.75f});
    }

    append(position, color);
}
