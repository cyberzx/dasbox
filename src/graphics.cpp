#include "globals.h"
#include "graphics.h"
#include "fileSystem.h"
#include <unordered_map>
#include <SFML/Graphics.hpp>
#include <daScript/daScript.h>
#include <daScript/simulate/interop.h>
#include <daScript/simulate/simulate_visit_op.h>
#include <string>

using namespace das;

#define SWAP_RB(c) (((c) & 0xFF00FF00) | (((c) & 0x000000FF) << 16) | (((c) & 0x00FF0000) >> 16))

static sf::RenderStates primitive_rs;
static sf::Font * font_mono = nullptr;
static sf::Font * font_sans = nullptr;
static sf::Font * current_font = nullptr;
static int current_font_size = 16;
static sf::Font * saved_font = nullptr;


const sf::BlendMode BlendPremultipliedAlpha(sf::BlendMode::One, sf::BlendMode::OneMinusSrcAlpha, sf::BlendMode::Add,
  sf::BlendMode::One, sf::BlendMode::OneMinusSrcAlpha, sf::BlendMode::Add);


int get_screen_width()
{
  return screen_width;
}

int get_screen_height()
{
  return screen_height;
}

int get_desktop_width()
{
  return sf::VideoMode::getDesktopMode().width;
}

int get_desktop_height()
{
  return sf::VideoMode::getDesktopMode().height;
}

sf::Color conv_color(uint32_t c)
{
  return sf::Color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
}

void fill_rect(float x, float y, float width, float height, uint32_t color)
{
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::TriangleStrip, 4);
  v[0].position = sf::Vector2f(x, y);
  v[0].color = c;
  v[1].position = sf::Vector2f(x, y + height);
  v[1].color = c;
  v[2].position = sf::Vector2f(x + width, y);
  v[2].color = c;
  v[3].position = sf::Vector2f(x + width, y + height);
  v[3].color = c;
  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void fill_rect_i(int x, int y, int width, int height, uint32_t color)
{
  fill_rect((float)x, (float)y, (float)width, (float)height, color);
}

void rect(float x, float y, float width, float height, uint32_t color)
{
  x += 0.5f;
  y += 0.5f;
  width -= 1.0f;
  height -= 1.0f;
  if (width < 0 || height < 0)
    return;
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::LineStrip, 5);
  v[0].position = sf::Vector2f(x, y);
  v[0].color = c;
  v[1].position = sf::Vector2f(x, y + height);
  v[1].color = c;
  v[2].position = sf::Vector2f(x + width, y + height);
  v[2].color = c;
  v[3].position = sf::Vector2f(x + width, y);
  v[3].color = c;
  v[4].position = sf::Vector2f(x, y);
  v[4].color = c;
  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void rect_i(int x, int y, int width, int height, uint32_t color)
{
  rect((float)x, (float)y, (float)width, (float)height, color);
}

void line(float x0, float y0, float x1, float y1, uint32_t color)  // TODO: cache
{
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::Lines, 2);
  v[0].position = sf::Vector2f(x0 + 0.5f, y0 + 0.5f);
  v[0].color = c;
  v[1].position = sf::Vector2f(x1 + 0.5f, y1 + 0.5f);
  v[1].color = c;
  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void line_i(int x0, int y0, int x1, int y1, uint32_t color)
{
  line((float)x0, (float)y0, (float)x1, (float)y1, color);
}

void set_pixel(float x, float y, uint32_t color) // TODO: cache
{
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::Points, 1);
  v[0].position = sf::Vector2f(x, y);// + 0.5f, y + 0.5f);
  v[0].color = c;
  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void set_pixel_i(int x, int y, uint32_t color)
{
  set_pixel((float)x, (float)y, color);
}

void circle(float x, float y, float radius, uint32_t color)
{
  if (radius < 0)
    return;

  if (radius <= 0.5f)
  {
    set_pixel(x, y, color);
    return;
  }

  x += 0.5f;
  y += 0.5f;

  int n = int(std::min(8.0f + std::max(radius - 2.0f, 0.0f) * 0.6f, 100.0f));

  sf::Color sfColor = conv_color(color);
  sf::VertexArray v(sf::LinesStrip, n + 1);

  float angleStep = float(M_PI) * 2.0f / n;

  for (int i = 0; i < n; i++)
  {
    float angle = angleStep * i;
    float s = sinf(angle);
    float c = cosf(angle);

    v[i].position = sf::Vector2f(x + s * radius, y + c * radius);
    v[i].color = sfColor;
  }

  v[n] = v[0];

  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void circle_i(int x, int y, int radius, uint32_t color)
{
  circle((float)x, (float)y, (float)radius, color);
}

void fill_circle(float x, float y, float radius, uint32_t color)
{
  if (radius < 0)
    return;

  if (radius < 0.5f)
  {
    set_pixel(x, y, color);
    return;
  }

  int n = int(std::min(8.0f + std::max(radius - 2.0f, 0.0f) * 0.6f, 100.0f));

  sf::Color sfColor = conv_color(color);
  sf::VertexArray v(sf::TriangleFan, n + 2);

  float angleStep = float(M_PI) * 2.0f / n;

  v[0].position = sf::Vector2f(x, y);
  v[0].color = sfColor;

  for (int i = 1; i <= n; i++)
  {
    float angle = angleStep * i;
    float s = sinf(angle);
    float c = cosf(angle);

    v[i].position = sf::Vector2f(x + s * radius, y + c * radius);
    v[i].color = sfColor;
  }

  v[n + 1] = v[1];

  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void fill_circle_i(int x, int y, int radius, uint32_t color)
{
  fill_circle((float)x, (float)y, (float)radius, color);
}

void set_font_name(const char * name)
{
  if (!name || !name[0] || !strcmp(name, "mono"))
  {
    current_font = font_mono;
    return;
  }

  if (!strcmp(name, "sans"))
  {
    current_font = font_sans;
    return;
  }

  // TODO: load font from file to font cache
  current_font = font_mono;
}

void stash_font()
{
  saved_font = current_font;
}

void restore_font()
{
  if (saved_font)
    current_font = saved_font;
  else
    current_font = font_mono;
}

void set_font_size(float size)
{
  current_font_size = int(size + 0.5f);
}

void set_font_size_i(int size)
{
  current_font_size = size;
}

int get_font_size_i()
{
  return int(current_font_size + 0.5f);
}

void text_out(float x, float y, const char * str, uint32_t color)
{
  if (!str || !str[0])
    return;
  sf::Color sfColor = conv_color(color);
  if (primitive_rs.blendMode == sf::BlendNone)
    sfColor.a = 255;
  sf::Text text;
  text.setFont(*current_font);
  text.setFillColor(sfColor);
  text.setPosition(x, y);
  text.setCharacterSize(current_font_size);
  text.setString(sf::String::fromUtf8(str, str + strlen(str)));
  if (g_render_target)
    g_render_target->draw(text, primitive_rs.blendMode == sf::BlendNone ? sf::BlendAlpha : primitive_rs.blendMode);
}

void text_out_i(int x, int y, const char * str, uint32_t color)
{
  text_out((float)x, (float)y, str, color);
}

static std::unordered_map<int, std::pair<float2, float2>> cached_char_size; // font_size / char_size, additional_size

das::float2 get_text_size(const char * str)
{
  if (!str || !str[0])
    return float2(0);

  float2 charSize = float2(0);
  float2 charBase = float2(0);
  auto it = cached_char_size.find(current_font_size);
  if (it == cached_char_size.end())
  {
    sf::Text text;
    text.setFont(*current_font);
    text.setCharacterSize(current_font_size);
    text.setPosition(0, 0);
    text.setString("W");
    sf::Vector2f base = text.findCharacterPos(0);
    text.setString("W\nWWW");
    sf::Vector2f last = text.findCharacterPos(3);
    charSize = float2(last.x - base.x, last.y - base.y);
    charBase = float2(base.x, base.y);
    cached_char_size[current_font_size] = std::make_pair(charSize, charBase);
  }
  else
  {
    charSize = it->second.first;
    charBase = it->second.second;
  }

  int len = int(strlen(str));
  int lines = 1;
  int maxLineLen = 0;
  int curLineLen = 0;
  for (int i = 0; i < len; i++)
  {
    if ((str[i] >= 32 && str[i] <= 127) || ((str[i] & 0xC0) == 0xC0))
      curLineLen++;
    else if (str[i] == '\n')
    {
      maxLineLen = std::max(curLineLen, maxLineLen);
      curLineLen = 0;
      lines++;
    }
    else if (str[i] == '\t')
      curLineLen += 4;
  }
  maxLineLen = std::max(curLineLen, maxLineLen);

  return das::float2(maxLineLen * charSize.x + charBase.x + 4, lines * charSize.y + charBase.y);
}

void enable_premultiplied_alpha_blend()
{
  primitive_rs.blendMode = BlendPremultipliedAlpha;
}

void enable_alpha_blend()
{
  primitive_rs.blendMode = sf::BlendAlpha;
}

void disable_alpha_blend()
{
  primitive_rs.blendMode = sf::BlendNone;
}

inline void polygon_internal(const das::float2 * points, int count, uint32_t color)
{
  if (count < 1 || count > 32768)
    return;

  if (count == 1)
  {
    set_pixel(points[0].x, points[0].y, color);
    return;
  }

  sf::Color sfColor = conv_color(color);
  sf::VertexArray v(sf::LinesStrip, count + 1);

  for (int i = 0; i < count; i++)
  {
    v[i].position = sf::Vector2f(points[i].x + 0.5f, points[i].y + 0.5f);
    v[i].color = sfColor;
  }

  v[count] = v[0];

  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}


void polygon(const das::TArray<das::float2> & points, uint32_t color)
{
  polygon_internal((const das::float2 *)points.data, int(points.size), color);
}



inline void fill_convex_polygon_internal(const das::float2 * points, int count, uint32_t color)
{
  if (count < 1 || count > 32768)
    return;

  if (count == 1)
  {
    set_pixel(points[0].x, points[0].y, color);
    return;
  }

  sf::Color sfColor = conv_color(color);
  sf::VertexArray v(sf::TriangleFan, count);

  for (int i = 0; i < count; i++)
  {
    v[i].position = sf::Vector2f(points[i].x, points[i].y);
    v[i].color = sfColor;
  }

  if (g_render_target)
    g_render_target->draw(v, primitive_rs);
}

void fill_convex_polygon(const das::TArray<das::float2> & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)points.data, int(points.size), color);
}


struct Image;
static unordered_set<sf::Image *> image_pointers;
static unordered_set<sf::Texture *> texture_pointers;


struct Image
{
  sf::Image * img;
  sf::Texture * tex;
  uint32_t * cached_pixels;
  int width;
  int height;
  bool applied;

  bool isValid() const
  {
    return !!img;
  }

  int getWidth() const
  {
    return width;
  }

  int getHeight() const
  {
    return height;
  }

  Image()
  {
    applied = false;
    img = nullptr;
    tex = nullptr;
    cached_pixels = nullptr;
    width = 0;
    height = 0;
  }

  Image(const Image & b)
  {
    img = b.img ? new sf::Image(*b.img) : nullptr;
    tex = b.tex ? new sf::Texture(*b.tex) : nullptr;
    cached_pixels = img ? (uint32_t *)img->getPixelsPtr() : nullptr;
    width = 0;
    height = 0;
    applied = false;
    image_pointers.insert(img);
    texture_pointers.insert(tex);
  }

  Image(Image && b)
  {
    img = b.img;
    tex = b.tex;
    cached_pixels = b.cached_pixels;
    width = b.width;
    height = b.height;
    applied = b.applied;

    b.width = 0;
    b.height = 0;
    b.img = nullptr;
    b.tex = nullptr;
    b.cached_pixels = nullptr;
  }

  Image& operator=(const Image & b)
  {
    image_pointers.erase(img);
    texture_pointers.erase(tex);
    delete img;
    delete tex;
    img = b.img ? new sf::Image(*b.img) : nullptr;
    tex = b.tex ? new sf::Texture(*b.tex) : nullptr;
    cached_pixels = img ? (uint32_t *)img->getPixelsPtr() : nullptr;
    width = b.width;
    height = b.height;
    applied = false;
    image_pointers.insert(img);
    texture_pointers.insert(tex);
    return *this;
  }

  Image& operator=(Image && b)
  {
    img = b.img;
    tex = b.tex;
    cached_pixels = b.cached_pixels;
    width = b.width;
    height = b.height;
    applied = b.applied;

    b.width = 0;
    b.height = 0;
    b.img = nullptr;
    b.tex = nullptr;
    b.cached_pixels = nullptr;
    return *this;
  }

  ~Image()
  {
    image_pointers.erase(img);
    texture_pointers.erase(tex);
    delete img;
    img = nullptr;
    delete tex;
    tex = nullptr;
    applied = false;
    cached_pixels = nullptr;
    width = 0;
    height = 0;
  }
};


void delete_image(Image * image)
{
  image_pointers.erase(image->img);
  texture_pointers.erase(image->tex);
  delete image->img;
  image->img = nullptr;
  delete image->tex;
  image->tex = nullptr;
  image->cached_pixels = nullptr;
  image->width = 0;
  image->height = 0;
  image->applied = false;
}

Image create_image_wh(int width, int height)
{
  if (width <= 0 || height <= 0)
    return Image();

  Image b;
  b.img = new sf::Image();
  b.img->create(width, height);

  b.cached_pixels = (uint32_t *)b.img->getPixelsPtr();
  b.width = width;
  b.height = height;

  b.tex = new sf::Texture();
  b.tex->loadFromImage(*b.img);

  image_pointers.insert(b.img);
  texture_pointers.insert(b.tex);

  return b;
}

Image create_image(int width, int height, const das::TArray<uint32_t> & pixels)
{
  if (width <= 0 || height <= 0)
    return Image();

  Image b;
  b.img = new sf::Image();
  image_pointers.insert(b.img);
  b.img->create(width, height);
  uint32_t *data = (uint32_t *)b.img->getPixelsPtr();
  int size = std::min(width * height, int(pixels.size));
  for (int i = 0; i < size; i++)
  {
    uint32_t c = pixels[i];
    data[i] = SWAP_RB(c);
  }

  b.cached_pixels = (uint32_t *)b.img->getPixelsPtr();
  b.width = width;
  b.height = height;

  b.tex = new sf::Texture();
  b.tex->loadFromImage(*b.img);
  texture_pointers.insert(b.tex);

  return b;
}

Image create_image_from_file(const char * file_name)
{
  if (!file_name || !*file_name)
  {
    print_error("Cannot open image. File name is empty.");
    return Image();
  }

  if (!fs::is_path_string_valid(file_name))
  {
    print_error("Cannot open image '%s'. Absolute paths or access to the parent directory is prohibited.", file_name);
    return Image();
  }

  Image b;
  b.img = new sf::Image();
  if (!b.img->loadFromFile(file_name))
  {
    fetch_cerr();
    //print_error("Cannot create image from file '%s'", file_name);
    delete b.img;
    b.img = nullptr;
    return Image();
  }

  b.cached_pixels = (uint32_t *)b.img->getPixelsPtr();
  b.width = b.img->getSize().x;
  b.height = b.img->getSize().y;

  b.tex = new sf::Texture();
  b.tex->loadFromImage(*b.img);

  image_pointers.insert(b.img);
  texture_pointers.insert(b.tex);

  return b;
}

void get_image_data(const Image & b, das::TArray<uint32_t> & out_pixels)
{
  if (!b.img)
    return;

  uint32_t count = b.width * b.height;
  if (count > out_pixels.size)
    count = out_pixels.size;

  if (count && b.cached_pixels)
  {
    int size = std::min(b.width * b.height, int(out_pixels.size));
    for (int i = 0; i < size; i++)
    {
      uint32_t c = b.cached_pixels[i];
      out_pixels.data[i] = SWAP_RB(c);
    }
  }
}

void set_image_data(Image & b, const das::TArray<uint32_t> & pixels)
{
  if (!b.img)
    return;

  b.applied = false;
  uint32_t count = b.width * b.height;
  if (count > pixels.size)
    count = pixels.size;

  if (count && b.cached_pixels)
  {
    int size = std::min(b.width * b.height, int(pixels.size));
    for (int i = 0; i < size; i++)
    {
      uint32_t c = pixels[i];
      b.cached_pixels[i] = SWAP_RB(c);
    }
  }
}

void set_image_pixel(Image & b, int x, int y, uint32_t color)
{
  if (x >= 0 && y >= 0 && x < b.width && y < b.height && b.cached_pixels)
  {
    b.applied = false;
    b.cached_pixels[y * b.width + x] = SWAP_RB(color);
  }
}

uint32_t get_image_pixel(const Image & b, int x, int y)
{
  if (x >= 0 && y >= 0 && x < b.width && y < b.height && b.cached_pixels)
  {
    uint32_t c = b.cached_pixels[y * b.width + x];
    return SWAP_RB(c);
  }
  else
    return 0;
}

void premultiply_alpha(Image & image)
{
  if (!image.cached_pixels)
    return;

  image.applied = false;
  int count = image.width * image.height;
  for (int i = 0; i < count; i++)
  {
    uint32_t p = image.cached_pixels[i];
    uint32_t a = (p >> 24) & 0xFFU;
    uint32_t b = (((p >> 0) & 0xFFU) * a) / 255U;
    uint32_t g = (((p >> 8) & 0xFFU) * a) / 255U;
    uint32_t r = (((p >> 16) & 0xFFU) * a) / 255U;
    image.cached_pixels[i] = (a << 24) | (r << 16) | (g << 8) | (b);
  }
}

void make_image_color_transparent(Image & image, uint32_t color)
{
  if (!image.cached_pixels)
    return;

  color = color & 0x00FFFFFF;
  color = SWAP_RB(color);
  image.applied = false;
  int count = image.width * image.height;
  for (int i = 0; i < count; i++)
  {
    uint32_t p = image.cached_pixels[i];
    if ((p & 0x00FFFFFF) == color)
      p &= 0x00FFFFFF;
    else
      p |= 0xFF000000;
    image.cached_pixels[i] = p;
  }
}

void set_image_smooth(Image & image, bool smooth)
{
  if (image.tex)
    image.tex->setSmooth(smooth);
}

void set_image_clamp(Image & image, bool clamp)
{
  if (image.tex)
    image.tex->setRepeated(!clamp);
}

void flip_image_x(Image & image)
{
  image.applied = false;
  if (image.img)
    image.img->flipHorizontally();
}

void flip_image_y(Image & image)
{
  image.applied = false;
  if (image.img)
    image.img->flipVertically();
}

inline void apply_texture(const Image & image)
{
  Image * b = (Image *)&image;
  b->applied = true;
  if (b->img->getSize() == b->tex->getSize())
    b->tex->update(*b->img);
  else
  {
    bool repeat = b->tex->isRepeated();
    bool smooth = b->tex->isSmooth();
    texture_pointers.erase(b->tex);
    delete b->tex;
    b->tex = new sf::Texture();
    texture_pointers.insert(b->tex);
    b->tex->setRepeated(repeat);
    b->tex->setSmooth(smooth);
    b->tex->loadFromImage(*b->img);
  }
}


void draw_image_cs2(const Image & image, float x, float y, uint32_t color, das::float2 size)
{
  if (!image.tex)
    return;
  if (!image.applied)
    apply_texture(image);
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::TriangleStrip, 4);
  v[0].position = sf::Vector2f(x, y);
  v[0].color = c;
  v[0].texCoords = sf::Vector2f(0, 0);
  v[1].position = sf::Vector2f(x, y + size.y);
  v[1].color = c;
  v[1].texCoords = sf::Vector2f(0, (float)image.height);
  v[2].position = sf::Vector2f(x + size.x, y);
  v[2].color = c;
  v[2].texCoords = sf::Vector2f((float)image.width, 0);
  v[3].position = sf::Vector2f(x + size.x, y + size.y);
  v[3].color = c;
  v[3].texCoords = sf::Vector2f((float)image.width, (float)image.height);

  sf::RenderStates states = primitive_rs;
  states.texture = image.tex;
  if (g_render_target)
    g_render_target->draw(v, states);
}

void draw_image(const Image & image, float x, float y)
{
  draw_image_cs2(image, x, y, 0xFFFFFFFF, das::float2(image.width, image.height));
}

void draw_image_c(const Image & image, float x, float y, uint32_t color)
{
  draw_image_cs2(image, x, y, color, das::float2(image.width, image.height));
}

void draw_image_cs(const Image & image, float x, float y, uint32_t color, float size)
{
  draw_image_cs2(image, x, y, color, das::float2(size, size));
}


void draw_image_cs2i(const Image & image, int x, int y, uint32_t color, das::int2 size)
{
  draw_image_cs2(image, (float)x, (float)y, color, das::float2(size.x, size.y));
}

void draw_image_i(const Image & image, int x, int y)
{
  draw_image_cs2(image, (float)x, (float)y, 0xFFFFFFFF, das::float2(image.width, image.height));
}

void draw_image_ci(const Image & image, int x, int y, uint32_t color)
{
  draw_image_cs2(image, (float)x, (float)y, color, das::float2(image.width, image.height));
}

void draw_image_csi(const Image & image, int x, int y, uint32_t color, int size)
{
  draw_image_cs2(image, (float)x, (float)y, color, das::float2(size, size));
}



void draw_quad(const Image & image, das::float2 p0, das::float2 p1, das::float2 p2, das::float2 p3, uint32_t color)
{
  if (!image.tex)
    return;
  if (!image.applied)
    apply_texture(image);
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::TriangleStrip, 4);
  v[0].position = sf::Vector2f(p0.x, p0.y);
  v[0].color = c;
  v[0].texCoords = sf::Vector2f(0, 0);
  v[1].position = sf::Vector2f(p1.x, p1.y);
  v[1].color = c;
  v[1].texCoords = sf::Vector2f(0, (float)image.height);
  v[2].position = sf::Vector2f(p3.x, p3.y);
  v[2].color = c;
  v[2].texCoords = sf::Vector2f((float)image.width, 0);
  v[3].position = sf::Vector2f(p2.x, p2.y);
  v[3].color = c;
  v[3].texCoords = sf::Vector2f((float)image.width, (float)image.height);
  sf::RenderStates states = primitive_rs;
  states.texture = image.tex;
  if (g_render_target)
    g_render_target->draw(v, states);

}

void draw_quad_a(const Image & image, das::float2 p[4], uint32_t color)
{
  if (!image.tex)
    return;
  if (!image.applied)
    apply_texture(image);
  sf::Color c = conv_color(color);
  sf::VertexArray v(sf::TriangleStrip, 4);
  v[0].position = sf::Vector2f(p[0].x, p[0].y);
  v[0].color = c;
  v[0].texCoords = sf::Vector2f(0, 0);
  v[1].position = sf::Vector2f(p[1].x, p[1].y);
  v[1].color = c;
  v[1].texCoords = sf::Vector2f(0, (float)image.height);
  v[2].position = sf::Vector2f(p[3].x, p[3].y);
  v[2].color = c;
  v[2].texCoords = sf::Vector2f((float)image.width, 0);
  v[3].position = sf::Vector2f(p[2].x, p[2].y);
  v[3].color = c;
  v[3].texCoords = sf::Vector2f((float)image.width, (float)image.height);
  sf::RenderStates states = primitive_rs;
  states.texture = image.tex;
  if (g_render_target)
    g_render_target->draw(v, states);

}

void draw_triangle_strip_color(const Image & image,
  const das::TArray<das::float2> & coord, const das::TArray<das::float2> & uv, uint32_t color)
{
  if (!image.tex)
    return;
  if (!image.applied)
    apply_texture(image);
  sf::Color c = conv_color(color);
  int count = std::min(coord.size, uv.size);
  if (count < 2)
    return;

  sf::VertexArray v(sf::TriangleStrip, count);
  for (int i = 0; i < count; i++)
  {
    v[i].position = sf::Vector2f(coord[i].x, coord[i].y);
    v[i].color = c;
    v[i].texCoords = sf::Vector2f(uv[i].x, uv[i].y);
  }

  sf::RenderStates states = primitive_rs;
  states.texture = image.tex;
  if (g_render_target)
    g_render_target->draw(v, states);

}

void draw_triangle_strip_color_a(const Image & image,
  const das::TArray<das::float2> & coord, const das::TArray<das::float2> & uv, const das::TArray<uint32_t> & colors)
{
  if (!image.tex)
    return;
  if (!image.applied)
    apply_texture(image);
  int count = std::min(coord.size, uv.size);
  if (count < 2)
    return;

  sf::VertexArray v(sf::TriangleStrip, count);
  for (int i = 0; i < count; i++)
  {
    v[i].position = sf::Vector2f(coord[i].x, coord[i].y);
    v[i].color = conv_color(colors[i]);
    v[i].texCoords = sf::Vector2f(uv[i].x, uv[i].y);
  }

  sf::RenderStates states = primitive_rs;
  states.texture = image.tex;
  if (g_render_target)
    g_render_target->draw(v, states);

}

void draw_triangle_strip(const Image & image,
  const das::TArray<das::float2> & coord, const das::TArray<das::float2> & uv)
{
  draw_triangle_strip_color(image, coord, uv, 0xFFFFFFFF);
}


typedef das::float2 PointsType_2[2];
typedef das::float2 PointsType_3[3];
typedef das::float2 PointsType_4[4];
typedef das::float2 PointsType_5[5];
typedef das::float2 PointsType_6[6];
typedef das::float2 PointsType_7[7];
typedef das::float2 PointsType_8[8];

void polygon2(const PointsType_2 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 2, color); }
void polygon3(const PointsType_3 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 3, color); }
void polygon4(const PointsType_4 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 4, color); }
void polygon5(const PointsType_5 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 5, color); }
void polygon6(const PointsType_6 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 6, color); }
void polygon7(const PointsType_7 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 7, color); }
void polygon8(const PointsType_8 & points, uint32_t color) { polygon_internal((const das::float2 *)&points, 8, color); }

void fill_convex_polygon2(const PointsType_2 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 2, color);
}

void fill_convex_polygon3(const PointsType_3 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 3, color);
}

void fill_convex_polygon4(const PointsType_4 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 4, color);
}

void fill_convex_polygon5(const PointsType_5 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 5, color);
}

void fill_convex_polygon6(const PointsType_6 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 6, color);
}

void fill_convex_polygon7(const PointsType_7 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 7, color);
}

void fill_convex_polygon8(const PointsType_8 & points, uint32_t color)
{
  fill_convex_polygon_internal((const das::float2 *)&points, 8, color);
}


static const uint8_t font_mono_data[] =
{
#include "resources/font.JetBrainsMonoNL-Medium.ttf.inl"
};

static const uint8_t font_sans_data[] =
{
#include "resources/font.OpenSans-Regular.ttf.inl"
};

namespace graphics
{

void initialize()
{
  font_mono = new sf::Font;
  if (!font_mono->loadFromMemory((void *)font_mono_data, sizeof(font_mono_data)))
    print_error("Cannot load default font (mono)\n");

  font_sans = new sf::Font;
  if (!font_sans->loadFromMemory((void *)font_sans_data, sizeof(font_sans_data)))
    print_error("Cannot load default font (sans)\n");

  saved_font = nullptr;
  set_font_name(nullptr);
}

void delete_allocated_images()
{
  for (auto && texture : texture_pointers)
    delete texture;
  texture_pointers.clear();

  for (auto && image : image_pointers)
    delete image;
  image_pointers.clear();
}

void finalize()
{
  delete font_mono;
  delete font_sans;
}

void on_graphics_frame_start()
{
  g_render_target->clear();
  g_render_target->resetGLStates();
  disable_alpha_blend();
}

} // namespace


MAKE_TYPE_FACTORY(Image, Image)


struct SimNode_DeleteImage : SimNode_Delete
{
  SimNode_DeleteImage( const LineInfo & a, SimNode * s, uint32_t t )
    : SimNode_Delete(a, s, t) {}

  virtual SimNode * visit(SimVisitor & vis) override
  {
    V_BEGIN();
    V_OP(DeleteImage);
    V_ARG(total);
    V_SUB(subexpr);
    V_END();
  }

  virtual vec4f eval(Context & context) override
  {
    DAS_PROFILE_NODE
    auto pH = (Image *)subexpr->evalPtr(context);
    for (uint32_t i = 0; i != total; ++i, pH++)
      delete_image(pH);
    return v_zero();
  }
};


struct ImageAnnotation : ManagedStructureAnnotation<Image, true, true>
{
  ImageAnnotation(ModuleLibrary & ml) : ManagedStructureAnnotation("Image", ml)
  {
    addProperty<DAS_BIND_MANAGED_PROP(getWidth)>("width");
    addProperty<DAS_BIND_MANAGED_PROP(getHeight)>("height");
    addProperty<DAS_BIND_MANAGED_PROP(isValid)>("valid");
  }

  bool canCopy() const override { return false; }
  virtual bool hasNonTrivialCtor() const override { return false; }
  virtual bool isLocal() const override { return true; }
  virtual bool canClone() const override { return true; }
  virtual bool canMove() const override { return true; }
  virtual bool canNew() const override { return true; }
  virtual bool canDelete() const override { return true; }
  virtual bool needDelete() const override { return true; }
  virtual bool canBePlacedInContainer() const override { return true; }

  virtual SimNode * simulateDelete(Context & context, const LineInfo & at, SimNode * sube, uint32_t count) const override
  {
    return context.code->makeNode<SimNode_DeleteImage>(at, sube, count);
  }
};


static char graphics_das[] =
#include "graphics.das.inl"
;


class ModuleGraphics : public Module
{
public:
  ModuleGraphics() : Module("graphics")
  {
    ModuleLibrary lib;
    lib.addModule(this);
    lib.addBuiltInModule();

    addAnnotation(das::make_smart<ImageAnnotation>(lib));
    addCtorAndUsing<Image>(*this, lib, "Image", "Image");

    addExtern<DAS_BIND_FUN(get_screen_width)>(*this, lib, "get_screen_width", SideEffects::accessExternal, "get_screen_width");
    addExtern<DAS_BIND_FUN(get_screen_height)>(*this, lib, "get_screen_height", SideEffects::accessExternal, "get_screen_height");
    addExtern<DAS_BIND_FUN(get_desktop_width)>(*this, lib, "get_desktop_width", SideEffects::accessExternal, "get_desktop_width");
    addExtern<DAS_BIND_FUN(get_desktop_height)>(*this, lib, "get_desktop_height", SideEffects::accessExternal, "get_desktop_height");
    addExtern<DAS_BIND_FUN(set_pixel)>(*this, lib, "set_pixel", SideEffects::modifyExternal, "set_pixel")
      ->args({"x", "y", "color"});

    addExtern<DAS_BIND_FUN(set_pixel_i)>(*this, lib, "set_pixel", SideEffects::modifyExternal, "set_pixel_i")
      ->args({"x", "y", "color"});

    addExtern<DAS_BIND_FUN(fill_rect)>(*this, lib, "fill_rect", SideEffects::modifyExternal, "fill_rect")
      ->args({"x", "y", "width", "height", "color"});

    addExtern<DAS_BIND_FUN(fill_rect_i)>(*this, lib, "fill_rect", SideEffects::modifyExternal, "fill_rect_i")
      ->args({"x", "y", "width", "height", "color"});

    addExtern<DAS_BIND_FUN(rect)>(*this, lib, "rect", SideEffects::modifyExternal, "rect")
      ->args({"x", "y", "width", "height", "color"});

    addExtern<DAS_BIND_FUN(rect_i)>(*this, lib, "rect", SideEffects::modifyExternal, "rect_i")
      ->args({"x", "y", "width", "height", "color"});

    addExtern<DAS_BIND_FUN(text_out)>(*this, lib, "text_out", SideEffects::modifyExternal, "text_out")
      ->args({"x", "y", "str", "color"});

    addExtern<DAS_BIND_FUN(text_out_i)>(*this, lib, "text_out", SideEffects::modifyExternal, "text_out_i")
      ->args({"x", "y", "str", "color"});

    addExtern<DAS_BIND_FUN(get_text_size)>(*this, lib, "get_text_size", SideEffects::modifyExternal, "get_text_size")
      ->args({"str"});

    addExtern<DAS_BIND_FUN(line)>(*this, lib, "line", SideEffects::modifyExternal, "line")
      ->args({"x0", "y0", "x1", "y1", "color"});

    addExtern<DAS_BIND_FUN(line_i)>(*this, lib, "line", SideEffects::modifyExternal, "line_i")
      ->args({"x0", "y0", "x1", "y1", "color"});

    addExtern<DAS_BIND_FUN(circle)>(*this, lib, "circle", SideEffects::modifyExternal, "circle")
      ->args({"x", "y", "radius", "color"});

    addExtern<DAS_BIND_FUN(circle_i)>(*this, lib, "circle", SideEffects::modifyExternal, "circle_i")
      ->args({"x", "y", "radius", "color"});

    addExtern<DAS_BIND_FUN(fill_circle)>(*this, lib, "fill_circle", SideEffects::modifyExternal, "fill_circle")
      ->args({"x", "y", "radius", "color"});

    addExtern<DAS_BIND_FUN(fill_circle_i)>(*this, lib, "fill_circle", SideEffects::modifyExternal, "fill_circle_i")
      ->args({"x", "y", "radius", "color"});

    addExtern<DAS_BIND_FUN(polygon)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon")
      ->args({"points", "color"});

    addExtern<DAS_BIND_FUN(polygon2)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon2");

    addExtern<DAS_BIND_FUN(polygon3)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon3");

    addExtern<DAS_BIND_FUN(polygon4)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon4");

    addExtern<DAS_BIND_FUN(polygon5)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon5");

    addExtern<DAS_BIND_FUN(polygon6)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon6");

    addExtern<DAS_BIND_FUN(polygon7)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon7");

    addExtern<DAS_BIND_FUN(polygon8)>(*this, lib, "polygon", SideEffects::modifyExternal, "polygon8");

    addExtern<DAS_BIND_FUN(fill_convex_polygon)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon")
      ->args({"points", "color"});

    addExtern<DAS_BIND_FUN(fill_convex_polygon2)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon2");
    addExtern<DAS_BIND_FUN(fill_convex_polygon3)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon3");
    addExtern<DAS_BIND_FUN(fill_convex_polygon4)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon4");
    addExtern<DAS_BIND_FUN(fill_convex_polygon5)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon5");
    addExtern<DAS_BIND_FUN(fill_convex_polygon6)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon6");
    addExtern<DAS_BIND_FUN(fill_convex_polygon7)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon7");
    addExtern<DAS_BIND_FUN(fill_convex_polygon8)>(*this, lib,
      "fill_convex_polygon", SideEffects::modifyExternal, "fill_convex_polygon8");


    addExtern<DAS_BIND_FUN(set_font_name)>(*this, lib, "set_font_name", SideEffects::modifyExternal, "set_font_name")
      ->args({"font_name"});

    addExtern<DAS_BIND_FUN(set_font_size)>(*this, lib, "set_font_size", SideEffects::modifyExternal, "set_font_size")
      ->args({"size_px"});

    addExtern<DAS_BIND_FUN(set_font_size_i)>(*this, lib, "set_font_size", SideEffects::modifyExternal, "set_font_size_i")
      ->args({"size_px"});

    addExtern<DAS_BIND_FUN(enable_premultiplied_alpha_blend)>(*this, lib,
      "enable_premultiplied_alpha_blend", SideEffects::modifyExternal, "enable_premultiplied_alpha_blend");
    addExtern<DAS_BIND_FUN(enable_alpha_blend)>(*this, lib,
      "enable_alpha_blend", SideEffects::modifyExternal, "enable_alpha_blend");
    addExtern<DAS_BIND_FUN(disable_alpha_blend)>(*this, lib,
      "disable_alpha_blend", SideEffects::modifyExternal, "disable_alpha_blend");

    addExtern<DAS_BIND_FUN(flip_image_x)>(*this, lib, "flip_image_x", SideEffects::modifyExternal, "flip_image_x");
    addExtern<DAS_BIND_FUN(flip_image_y)>(*this, lib, "flip_image_y", SideEffects::modifyExternal, "flip_image_y");
    addExtern<DAS_BIND_FUN(set_image_smooth)>(*this, lib, "set_image_smooth", SideEffects::modifyExternal, "set_image_smooth")
      ->args({"image", "is_smooth"});

    addExtern<DAS_BIND_FUN(set_image_clamp)>(*this, lib, "set_image_clamp", SideEffects::modifyExternal, "set_image_clamp")
      ->args({"image", "is_clamped"});


    addExtern<DAS_BIND_FUN(create_image_wh), SimNode_ExtFuncCallAndCopyOrMove>
      (*this, lib, "create_image", SideEffects::modifyExternal, "create_image_wh")
      ->args({"width", "height"});

    addExtern<DAS_BIND_FUN(create_image), SimNode_ExtFuncCallAndCopyOrMove>
      (*this, lib, "create_image", SideEffects::modifyExternal, "create_image")
      ->args({"width", "height", "pixels"});

    addExtern<DAS_BIND_FUN(create_image_from_file), SimNode_ExtFuncCallAndCopyOrMove>(*this, lib,
      "create_image", SideEffects::modifyExternal, "create_image_from_file")
      ->args({"file_name"});

    addExtern<DAS_BIND_FUN(draw_quad)>(*this, lib, "draw_quad", SideEffects::modifyExternal, "draw_quad")
      ->args({"image", "p0", "p1", "p2", "p3", "color"});

    addExtern<DAS_BIND_FUN(draw_quad_a)>(*this, lib, "draw_quad", SideEffects::modifyExternal, "draw_quad_a")
      ->args({"image", "points", "color"});

    addExtern<DAS_BIND_FUN(draw_triangle_strip)>(*this, lib,
      "draw_triangle_strip", SideEffects::modifyExternal, "draw_triangle_strip")
      ->args({"image", "coord", "uv"});

    addExtern<DAS_BIND_FUN(draw_triangle_strip_color)>(*this, lib,
      "draw_triangle_strip", SideEffects::modifyExternal, "draw_triangle_strip_color")
      ->args({"image", "coord", "uv", "color"});

    addExtern<DAS_BIND_FUN(draw_triangle_strip_color_a)>(*this, lib,
      "draw_triangle_strip", SideEffects::modifyExternal, "draw_triangle_strip_color_a")
      ->args({"image", "coord", "uv", "colors"});


    addExtern<DAS_BIND_FUN(draw_image)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image")
      ->args({"image", "x", "y"});

    addExtern<DAS_BIND_FUN(draw_image_c)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_c")
      ->args({"image", "x", "y", "color"});

    addExtern<DAS_BIND_FUN(draw_image_cs)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_cs")
      ->args({"image", "x", "y", "color", "size"});

    addExtern<DAS_BIND_FUN(draw_image_cs2)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_cs2")
      ->args({"image", "x", "y", "color", "size"});

    addExtern<DAS_BIND_FUN(draw_image_i)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_i")
      ->args({"image", "x", "y"});

    addExtern<DAS_BIND_FUN(draw_image_ci)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_ci")
      ->args({"image", "x", "y", "color"});

    addExtern<DAS_BIND_FUN(draw_image_csi)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_csi")
      ->args({"image", "x", "y", "color", "size"});

    addExtern<DAS_BIND_FUN(draw_image_cs2i)>(*this, lib, "draw_image", SideEffects::modifyExternal, "draw_image_cs2i")
      ->args({"image", "x", "y", "color", "size"});

    addExtern<DAS_BIND_FUN(premultiply_alpha)>(*this, lib,
      "premultiply_alpha", SideEffects::modifyExternal, "premultiply_alpha")
      ->args({"image"});

    addExtern<DAS_BIND_FUN(make_image_color_transparent)>(*this, lib,
      "make_image_color_transparent", SideEffects::modifyExternal, "make_image_color_transparent")
      ->args({"image", "color"});

    addExtern<DAS_BIND_FUN(get_image_data)>(*this, lib,
      "get_image_data", SideEffects::modifyArgumentAndExternal, "get_image_data")
      ->args({"image", "out_pixels"});

    addExtern<DAS_BIND_FUN(set_image_data)>(*this, lib, "set_image_data", SideEffects::modifyExternal, "set_image_data")
      ->args({"image", "pixels"});

    addExtern<DAS_BIND_FUN(set_image_pixel)>(*this, lib, "set_pixel", SideEffects::modifyExternal, "set_pixel")
      ->args({"image", "x", "y", "color"});

    addExtern<DAS_BIND_FUN(get_image_pixel)>(*this, lib, "get_pixel", SideEffects::accessExternal, "get_pixel")
      ->args({"image", "x", "y"});


    compileBuiltinModule("graphics.das", (unsigned char *)graphics_das, sizeof(graphics_das));

    //verifyAotReady();
  }

  virtual ModuleAotType aotRequire(TextWriter & tw) const override
  {
    return ModuleAotType::cpp;
  }
};

REGISTER_MODULE(ModuleGraphics);
