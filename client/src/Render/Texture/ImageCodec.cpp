#include "ImageCodec.h"
#include <cstdlib>
#include <cstring>
#include <climits>
#include <memory>

#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "../../../lib/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../lib/stb/stb_image_write.h"

namespace ImageCodec {
Image::~Image() { stbi_image_free(pixels); }

int PaddedExtent(int size, int maxSize)
{
    if (size <= 0 || size > maxSize) return 0;
    int padded = 1;
    while (padded < size) {
        if (padded > maxSize / 2) return 0;
        padded *= 2;
    }
    return padded <= maxSize ? padded : 0;
}

bool Decode(FILE* file, int prefix, Format format, int maxSize, Image& out)
{
    if (!file || prefix < 0 || maxSize <= 0 || fseek(file, 0, SEEK_END)) return false;
    const long end = ftell(file);
    // FIX 2026-11-12 (v2.4, rev. 2): los .ozj distribuidos traen SOI (FF D8)
    // TANTO en 0 como en 24, pero el JPEG completo y valido es el de 24: el
    // stream de 0 es solo la cabecera JFIF con miniatura y stb no lo
    // decodifica (verificado con ffprobe: Loading01.ozj y Player/*.OZJ solo
    // decodifican desde 24; Skill.ozj decodifica igual desde 0 y desde 24).
    // La rev. 1 forzaba prefix=0 al ver SOI en 0 y dejo sin textura el
    // titulo, los personajes y el mundo. Regla: manda el SOI en 24; el 0 es
    // solo para JPEGs crudos sin SOI en 24; 32, ultimo recurso.
    // (Nota historica de la rev. 1:) parte de los .ozj distribuidos son JPEG crudos,
    // sin la cabecera Webzen de 24/32 bytes que el llamador asume al pasar
    // `prefix` (p.ej. Data/Interface/Skill.ozj y Level_01.ozj arrancan con
    // FF D8 en el offset 0; byte-verificado). Saltar el prefix decapitaba el
    // JPEG: los iconos de skill salian oscuros y el cromo de la barra
    // inferior corrupto. Si el archivo YA arranca con el SOI del JPEG se
    // decodifica desde el byte 0; el prefix solo se respeta en archivos
    // con cabecera real.
    if (format == Format::Jpeg && end >= 34) {
        auto soiAt = [&](long off) {
            if (off < 0 || end < off + 2 || fseek(file, off, SEEK_SET)) return false;
            unsigned char b[2] = { 0, 0 };
            return fread(b, 1, 2, file) == 2 && b[0] == 0xFF && b[1] == 0xD8;
        };
        if (soiAt(24))
            prefix = 24;
        else if (!soiAt(prefix) && soiAt(0))
            prefix = 0;
        else if (!soiAt(prefix) && soiAt(32))
            prefix = 32;
    }
    if (end <= prefix || end - prefix > INT_MAX - 2 || fseek(file, prefix, SEEK_SET)) return false;
    size_t length = static_cast<size_t>(end - prefix);
    std::unique_ptr<unsigned char, decltype(&free)> bytes(
        static_cast<unsigned char*>(malloc(length + 2)), &free);
    if (!bytes || fread(bytes.get(), 1, length, file) != length) return false;
    return DecodeBuffer(bytes.get(), length, format, maxSize, out);
}

bool DecodeBuffer(unsigned char* data, size_t length, Format format, int maxSize, Image& out)
{
    if (!data || maxSize <= 0) return false;
    const int channels = format == Format::Jpeg ? 3 : 4;
    if (format == Format::Jpeg) {
        if (length < 2 || data[0] != 0xff || data[1] != 0xd8) return false;
        // IJG's stdio source injects EOI at EOF (JWRN_JPEG_EOF). Keep that
        // recovery: stb otherwise fills an unfinished final scan differently.
        if (data[length - 2] != 0xff || data[length - 1] != 0xd9) {
            data[length++] = 0xff;
            data[length++] = 0xd9;
        }
    } else {
        if (length < 18 || data[0] != 0 || data[1] != 0 || data[2] != 2 || data[16] != 32)
            return false;
        const int width = data[12] | (data[13] << 8);
        const int height = data[14] | (data[15] << 8);
        if (!PaddedExtent(width, maxSize) || !PaddedExtent(height, maxSize) ||
            static_cast<size_t>(width) * height > (length - 18) / 4) return false;
        // The legacy loader always reversed storage rows, ignoring origin bits.
        // Normalize the local header only; assets on disk are never modified.
        data[17] &= ~0x30;
    }
    int width = 0, height = 0, sourceChannels = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(length), &width, &height, &sourceChannels) ||
        !PaddedExtent(width, maxSize) || !PaddedExtent(height, maxSize)) return false;
    const size_t paddedWidth = static_cast<size_t>(PaddedExtent(width, maxSize));
    const size_t paddedHeight = static_cast<size_t>(PaddedExtent(height, maxSize));
    if (paddedWidth > static_cast<size_t>(INT_MAX) / channels / paddedHeight) return false;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(length),
        &width, &height, &sourceChannels, channels);
    if (!pixels) return false;
    stbi_image_free(out.pixels);
    out.pixels = pixels;
    out.width = width; out.height = height; out.channels = channels;
    return true;
}

bool DecodePng(FILE* file, int channels, int maxSize, Image& out)
{
    if (!file || (channels != 3 && channels != 4) || maxSize <= 0 || fseek(file, 0, SEEK_END)) return false;
    const long end = ftell(file);
    if (end <= 8 || end > INT_MAX || fseek(file, 0, SEEK_SET)) return false;
    std::unique_ptr<unsigned char, decltype(&free)> bytes(static_cast<unsigned char*>(malloc(size_t(end))), &free);
    if (!bytes || fread(bytes.get(), 1, size_t(end), file) != size_t(end)) return false;
    return DecodePngBuffer(bytes.get(), size_t(end), channels, maxSize, out);
}

bool DecodePngBuffer(const unsigned char* data, size_t length, int channels, int maxSize, Image& out)
{
    if (!data || length <= 8 || length > INT_MAX || (channels != 3 && channels != 4) || maxSize <= 0) return false;
    const int end = int(length);
    static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (memcmp(data, signature, 8)) return false;
    int width = 0, height = 0, sourceChannels = 0;
    if (!stbi_info_from_memory(data, int(end), &width, &height, &sourceChannels) ||
        width <= 0 || height <= 0 || width > maxSize || height > maxSize) return false;
    unsigned char* pixels = stbi_load_from_memory(data, int(end), &width, &height, &sourceChannels, channels);
    if (!pixels) return false;
    stbi_image_free(out.pixels);
    out.pixels = pixels;
    out.width = width; out.height = height; out.channels = channels;
    return true;
}

struct Writer { FILE* file; bool ok; };
static void Write(void* context, void* data, int size)
{
    Writer* writer = static_cast<Writer*>(context);
    if (writer->ok && fwrite(data, 1, size, writer->file) != static_cast<size_t>(size))
        writer->ok = false;
}
bool WriteJpeg(FILE* file, int width, int height, const void* framebuffer, int quality)
{
    if (!file || !framebuffer || width <= 0 || height <= 0 || width > 65535 || height > 65535 ||
        static_cast<size_t>(width) * height > static_cast<size_t>(INT_MAX) / 3) return false;
    const size_t stride = static_cast<size_t>(width) * 3;
    std::unique_ptr<unsigned char, decltype(&free)> topDown(
        static_cast<unsigned char*>(malloc(stride * height)), &free);
    if (!topDown) return false;
    for (int y = 0; y < height; ++y)
        memcpy(topDown.get() + y * stride,
            static_cast<const unsigned char*>(framebuffer) + (height - 1 - y) * stride, stride);
    // IJG clamps to 1..100; stb's special quality=0 default must not replace it.
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    Writer writer = { file, true };
    const int result = stbi_write_jpg_to_func(Write, &writer, width, height, 3, topDown.get(), quality);
    return result != 0 && writer.ok && !ferror(file);
}
}
