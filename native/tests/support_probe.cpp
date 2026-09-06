// Behavioral checks for the source-built Android support stack on the OHOS host.
#include <android-base/file.h>
#include <androidfw/ApkAssets.h>
#include <cutils/ashmem.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <ziparchive/zip_archive.h>
#include <expat.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ot.h>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line=%d: %s errno=%d\n", __LINE__, #x, errno); return 1; } } while (0)

static int elements = 0;
static void element(void*, const char*, const char**) { ++elements; }

int main(int argc, char** argv) {
    CHECK(argc == 6); // ZIP fixture, PNG fixture, font, original APK, malformed ZIP
    int fd = ashmem_create_region("a2hlab-probe", 32768);
    CHECK(fd >= 0 && ashmem_valid(fd) && ashmem_get_size_region(fd) == 32768);
    auto* memory = static_cast<char*>(mmap(nullptr, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    CHECK(memory != MAP_FAILED);
    strcpy(memory, "parent");
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        if (strcmp(memory, "parent")) _exit(2);
        strcpy(memory, "child");
        _exit(0);
    }
    int status = -1;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(strcmp(memory, "child") == 0);
    CHECK(ashmem_set_prot_region(fd, PROT_READ) == 0);
    CHECK(ashmem_set_prot_region(fd, PROT_READ | PROT_WRITE) == -1);
    void* denied = mmap(nullptr, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(denied == MAP_FAILED);
    strcpy(memory, "existing mapping remains writable");
    auto* readonly = static_cast<char*>(mmap(nullptr, 32768, PROT_READ, MAP_SHARED, fd, 0));
    CHECK(readonly != MAP_FAILED && strcmp(readonly, memory) == 0);
    CHECK(ashmem_pin_region(fd, 0, 0) == 0 && ashmem_unpin_region(fd, 0, 0) == 0);
    munmap(readonly, 32768); munmap(memory, 32768); close(fd);
    CHECK(!ashmem_valid(-1) && ashmem_get_size_region(-1) == -1);
    puts("PASS ashmem shared-across-fork read-only-seal invalid-fd");

    const char* phrase = "你好 Android";
    android::String16 utf16(phrase);
    android::String8 utf8(utf16);
    CHECK(strcmp(utf8.c_str(), phrase) == 0);
    ZipArchiveHandle archive = nullptr;
    CHECK(OpenArchive(argv[1], &archive) == 0);
    ZipEntry entry;
    CHECK(FindEntry(archive, "hello.txt", &entry) == 0);
    std::vector<uint8_t> bytes(entry.uncompressed_length);
    CHECK(ExtractToMemory(archive, &entry, bytes.data(), bytes.size()) == 0);
    CHECK(std::string(reinterpret_cast<char*>(bytes.data()), bytes.size()) == phrase);
    CHECK(FindEntry(archive, "absent", &entry) != 0);
    CloseArchive(archive);
    CHECK(OpenArchive(argv[5], &archive) != 0);
    puts("PASS zip deflate-extraction missing-entry malformed-archive strings Chinese-roundtrip");

    XML_Parser xml = XML_ParserCreateNS(nullptr, ':');
    CHECK(xml);
    XML_SetStartElementHandler(xml, element);
    const char* document = "<feed xmlns='urn:a2hlab'><title>你好</title></feed>";
    CHECK(XML_Parse(xml, document, strlen(document), XML_TRUE) == XML_STATUS_OK && elements == 2);
    XML_ParserFree(xml);
    xml = XML_ParserCreate(nullptr);
    CHECK(XML_Parse(xml, "<a></b>", 7, XML_TRUE) == XML_STATUS_ERROR);
    XML_ParserFree(xml);
    puts("PASS XML namespaces Unicode malformed-rejection");

    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    CHECK(png_image_begin_read_from_file(&image, argv[2]));
    CHECK(image.width == 2 && image.height == 2);
    image.format = PNG_FORMAT_RGBA;
    uint8_t pixels[16];
    CHECK(png_image_finish_read(&image, nullptr, pixels, 0, nullptr));
    const uint8_t expected[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    CHECK(memcmp(pixels, expected, sizeof(expected)) == 0);
    png_image_free(&image);
    puts("PASS PNG reference-pixels");

    UErrorCode error = U_ZERO_ERROR;
    UChar text[32]; int32_t length = 0;
    u_strFromUTF8(text, 32, &length, phrase, -1, &error);
    CHECK(U_SUCCESS(error) && length == 10);
    UBreakIterator* breaker = ubrk_open(UBRK_WORD, "zh", text, length, &error);
    CHECK(U_SUCCESS(error) && breaker);
    int boundaries = 0;
    for (int p = ubrk_first(breaker); p != UBRK_DONE; p = ubrk_next(breaker)) ++boundaries;
    CHECK(boundaries >= 4);
    ubrk_close(breaker);
    puts("PASS ICU Chinese-conversion word-boundaries locale-data");

    FT_Library ft; FT_Face face;
    CHECK(FT_Init_FreeType(&ft) == 0 && FT_New_Face(ft, argv[3], 0, &face) == 0);
    CHECK(FT_Set_Pixel_Sizes(face, 0, 24) == 0);
    CHECK(FT_Get_Char_Index(face, 0x4f60) != 0);
    CHECK(FT_Load_Char(face, 0x4f60, FT_LOAD_RENDER) == 0);
    CHECK(face->glyph->bitmap.width > 0 && face->glyph->bitmap.rows > 0);
    FT_Done_Face(face); FT_Done_FreeType(ft);
    hb_blob_t* blob = hb_blob_create_from_file(argv[3]);
    CHECK(hb_blob_get_length(blob) > 0);
    hb_face_t* hbface = hb_face_create(blob, 0);
    hb_font_t* font = hb_font_create(hbface);
    hb_ot_font_set_funcs(font);
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, "你好", -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, nullptr, 0);
    unsigned count;
    auto* glyphs = hb_buffer_get_glyph_infos(buffer, &count);
    CHECK(count == 2 && glyphs[0].codepoint != 0 && glyphs[1].codepoint != 0);
    hb_buffer_destroy(buffer); hb_font_destroy(font); hb_face_destroy(hbface); hb_blob_destroy(blob);
    CHECK(dlopen("libminikin.so", RTLD_NOW | RTLD_LOCAL));
    CHECK(dlopen("libnativehelper.so", RTLD_NOW | RTLD_LOCAL));
    puts("PASS fonts Chinese-raster Chinese-shaping minikin-nativehelper-load");

    auto apk = android::ApkAssets::Load(argv[4]);
    CHECK(apk && apk->GetLoadedArsc());
    bool found = false;
    for (const auto& package : apk->GetLoadedArsc()->GetPackages()) {
        if (package->GetPackageName() == "com.ss.android.article.news") found = true;
    }
    CHECK(found);
    puts("PASS androidfw original-Toutiao-resource-table");
    puts("ANDROID_SUPPORT_PASS");
    return 0;
}
