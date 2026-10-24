// App-authored positional adapter; decoder is the separately pinned libchdr.
#include <jni.h>
#include <libchdr/chd.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr uint64_t kMaxFile = 2ULL * 1024 * 1024 * 1024;
constexpr uint32_t kMaxHunks = 262144;
constexpr size_t kIoChunk = 65536;

void fail(JNIEnv* env, const char* text) {
    if (env->ExceptionCheck()) return; // Preserve cancellation/provider exceptions.
    jclass type = env->FindClass("java/io/IOException");
    if (type) { env->ThrowNew(type, text); env->DeleteLocalRef(type); }
}
uint64_t be(const uint8_t* bytes, int count) {
    uint64_t value = 0;
    for (int i = 0; i < count; ++i) value = (value << 8) | bytes[i];
    return value;
}
struct Reader {
    JNIEnv* env = nullptr; // Refreshed on every serialized JNI call, never used asynchronously.
    jobject owner = nullptr;
    jbyteArray bytes = nullptr;
    jmethodID read = nullptr;
    uint64_t size = 0, position = 0;
    chd_file* chd = nullptr;
    std::vector<uint8_t> hunk;
    ~Reader() {
        if (chd) chd_close(chd);
        if (bytes) env->DeleteGlobalRef(bytes);
        if (owner) env->DeleteGlobalRef(owner);
    }
};
uint64_t sourceSize(void* context) { return static_cast<Reader*>(context)->size; }
int sourceClose(void*) { return 0; } // Java owns the original source/SAF grant.
int sourceSeek(void* context, int64_t offset, int origin) {
    auto* reader = static_cast<Reader*>(context);
    if (reader->env->ExceptionCheck()) return -1;
    uint64_t base;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = reader->position;
    else if (origin == SEEK_END) base = reader->size;
    else return -1;
    if (offset < 0) {
        uint64_t backwards = static_cast<uint64_t>(-(offset + 1)) + 1;
        if (backwards > base) return -1;
        reader->position = base - backwards;
    } else {
        if (static_cast<uint64_t>(offset) > reader->size - base) return -1;
        reader->position = base + static_cast<uint64_t>(offset);
    }
    return 0;
}
size_t sourceRead(void* destination, size_t width, size_t count, void* context) {
    auto* reader = static_cast<Reader*>(context);
    if (!width || !count || reader->env->ExceptionCheck()
            || count > SIZE_MAX / width) return 0;
    size_t requested = width * count;
    size_t done = 0;
    requested = static_cast<size_t>(std::min<uint64_t>(requested, reader->size - reader->position));
    while (done < requested) {
        auto chunk = static_cast<jint>(std::min(kIoChunk, requested - done));
        jint actual = reader->env->CallIntMethod(reader->owner, reader->read,
                static_cast<jlong>(reader->position), reader->bytes, 0, chunk);
        if (reader->env->ExceptionCheck()) return done / width;
        if (actual <= 0 || actual > chunk) {
            fail(reader->env, "CHD provider ended or made invalid read progress");
            return done / width;
        }
        reader->env->GetByteArrayRegion(reader->bytes, 0, actual,
                reinterpret_cast<jbyte*>(destination) + done);
        if (reader->env->ExceptionCheck()) return done / width;
        done += static_cast<size_t>(actual); reader->position += static_cast<uint64_t>(actual);
    }
    return done / width;
}
const core_file_callbacks callbacks = {sourceSize, sourceRead, sourceClose, sourceSeek};

bool preflight(Reader* reader) {
    uint8_t header[124]{};
    if (sourceRead(header, 1, sizeof(header), reader) != sizeof(header)) return false;
    if (std::memcmp(header, "MComprHD", 8) || be(header + 8, 4) != 124 || be(header + 12, 4) != 5) return false;
    uint64_t logical = be(header + 32, 8), map = be(header + 40, 8);
    uint64_t hunk = be(header + 56, 4), unit = be(header + 60, 4);
    if (!logical || logical > kMaxFile || unit != 2448 || logical % unit || !hunk
            || hunk > 512 * 1024 || hunk % unit || map < 124 || map >= reader->size) return false;
    for (int i = 104; i < 124; ++i) if (header[i]) return false;
    uint64_t hunks = (logical + hunk - 1) / hunk;
    if (hunks > kMaxHunks) return false;
    if (!be(header + 16, 4)) return hunks * 4 <= reader->size - map;
    uint8_t mapHeader[16]{};
    if (sourceSeek(reader, static_cast<int64_t>(map), SEEK_SET)
            || sourceRead(mapHeader, 1, sizeof(mapHeader), reader) != sizeof(mapHeader)) return false;
    uint64_t bytes = be(mapHeader, 4);
    // Bound compressed-map allocation before libchdr processes its entropy stream.
    return bytes > 0 && bytes <= 4 * 1024 * 1024 && reader->size - map >= 16
            && bytes <= reader->size - map - 16 && mapHeader[12] <= 24
            && mapHeader[13] <= 32 && mapHeader[14] <= 32;
}

bool safeMap(Reader* reader) {
    const chd_header* header = chd_get_header(reader->chd);
    if (!header || !header->rawmap || header->hunkcount > kMaxHunks) return false;
    std::vector<uint8_t> depth(header->hunkcount, 0);
    for (uint32_t i = 0; i < header->hunkcount; ++i) {
        const uint8_t* entry = header->rawmap + static_cast<size_t>(i) * header->mapentrybytes;
        if (header->compression[0] == 0) {
            uint64_t offset = be(entry, 4) * header->hunkbytes;
            // A zero NONE-map entry denotes a zero-filled hunk, not a missing parent.
            if (offset && (offset > reader->size || header->hunkbytes > reader->size - offset)) return false;
        } else {
            uint64_t offset = be(entry + 4, 6), length = be(entry + 1, 3);
            if (entry[0] <= 4) {
                if ((entry[0] < 4 && !header->compression[entry[0]]) || !length
                        || length > header->hunkbytes || offset > reader->size
                        || length > reader->size - offset) return false;
                if (entry[0] == 4 && length != header->hunkbytes) return false;
            } else if (entry[0] == 5) {
                // Avoid cyclic/forward references and unbounded recursive hunk chains.
                if (offset >= i || depth[static_cast<size_t>(offset)] >= 32) return false;
                depth[i] = depth[static_cast<size_t>(offset)] + 1;
            } else return false; // Parent-dependent maps are deliberately unsupported.
        }
    }
    reader->hunk.resize(header->hunkbytes);
    return true;
}
} // namespace

#define METHOD(name) Java_com_mateussouza_emuorbit_advance_data_storage_playstation_PlayStationNativeChdSource_##name
extern "C" JNIEXPORT jlong JNICALL METHOD(nativeOpen)(JNIEnv* env, jobject self, jlong size) {
    if (size <= 0 || static_cast<uint64_t>(size) > kMaxFile) { fail(env, "CHD source exceeds budget"); return 0; }
    try {
        auto reader = std::make_unique<Reader>(); reader->env = env; reader->size = static_cast<uint64_t>(size);
        reader->owner = env->NewGlobalRef(self);
        if (!reader->owner || env->ExceptionCheck()) return 0;
        jbyteArray bytes = env->NewByteArray(kIoChunk);
        if (!bytes || env->ExceptionCheck()) return 0;
        if (bytes) { reader->bytes = static_cast<jbyteArray>(env->NewGlobalRef(bytes)); env->DeleteLocalRef(bytes); }
        if (!reader->bytes || env->ExceptionCheck()) return 0;
        jclass type = env->GetObjectClass(self);
        if (type) { reader->read = env->GetMethodID(type, "readCompressed", "(J[BII)I"); env->DeleteLocalRef(type); }
        if (!reader->owner || !reader->bytes || !reader->read || env->ExceptionCheck()) return 0;
        if (!preflight(reader.get())) { fail(env, "Invalid or oversized CHD header/map"); return 0; }
        if (chd_open_core_file_callbacks(&callbacks, reader.get(), CHD_OPEN_READ, nullptr, &reader->chd) != CHDERR_NONE
                || env->ExceptionCheck()) { fail(env, "CHD decoder could not open source"); return 0; }
        if (!safeMap(reader.get())) { fail(env, "CHD contains unsafe or unsupported hunk references"); return 0; }
        return reinterpret_cast<jlong>(reader.release());
    } catch (const std::bad_alloc&) { fail(env, "CHD decoder memory budget unavailable"); return 0; }
}
extern "C" JNIEXPORT void JNICALL METHOD(nativeReadHunk)(JNIEnv* env, jobject, jlong handle, jint index, jbyteArray output) {
    auto* reader = reinterpret_cast<Reader*>(handle);
    if (!reader) { fail(env, "CHD decoder closed"); return; }
    reader->env = env;
    const chd_header* header = chd_get_header(reader->chd);
    if (!output || index < 0 || static_cast<uint32_t>(index) >= header->hunkcount
            || env->GetArrayLength(output) != static_cast<jsize>(reader->hunk.size())) {
        fail(env, "Invalid CHD hunk destination/index"); return;
    }
    if (chd_read(reader->chd, static_cast<uint32_t>(index), reader->hunk.data()) != CHDERR_NONE
            || env->ExceptionCheck()) { fail(env, "CHD hunk decoding/integrity failure"); return; }
    env->SetByteArrayRegion(output, 0, static_cast<jsize>(reader->hunk.size()),
            reinterpret_cast<const jbyte*>(reader->hunk.data()));
}
extern "C" JNIEXPORT void JNICALL METHOD(nativeClose)(JNIEnv* env, jobject, jlong handle) {
    auto* reader = reinterpret_cast<Reader*>(handle);
    if (reader) { reader->env = env; delete reader; }
}
