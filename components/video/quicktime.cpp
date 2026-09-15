/*
 * QuickTime (.mov) parsing for MJPEG clips. See quicktime.h for what changed from the
 * Flash_PNG sketch's version.
 */
#include "quicktime.h"

#include <inttypes.h>
#include <string.h>

namespace quicktime {

namespace {

uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

template <typename T> std::unique_ptr<T> take(std::unique_ptr<Atom> &a)
{
    return std::unique_ptr<T>(static_cast<T *>(a.release()));
}

/* A table larger than this is damage, not a movie: a million frames is nine hours at 30 fps. */
constexpr uint32_t MAX_ENTRIES = 1u << 20;

/* Load `count` entries of `entry_size` bytes from `at` in one read, or fail if they would run
 * past the atom. */
bool read_table(const QTFile &file, uint64_t atom_offset, uint64_t atom_size, uint64_t at,
                uint32_t count, size_t entry_size, std::vector<uint8_t> &out)
{
    if (count > MAX_ENTRIES || at + (uint64_t)count * entry_size > atom_size) {
        return false;
    }
    out.resize((size_t)count * entry_size);
    return count == 0 || file.Read(atom_offset + at, out.data(), out.size());
}

void fourcc(uint32_t v, char out[5])
{
    for (int i = 0; i < 4; i++) {
        const char c = (char)(v >> (24 - 8 * i));
        out[i] = (c >= 0x20 && c < 0x7f) ? c : '?';
    }
    out[4] = '\0';
}

}  // namespace

/* ------------------------------------------------------------------ QTFile */

QTFile::QTFile(FILE *file) : mFile(file), mSize(0)
{
    if (mFile != nullptr && fseek(mFile, 0, SEEK_END) == 0) {
        const long end = ftell(mFile);
        mSize = end > 0 ? (uint64_t)end : 0;
    }
}

bool QTFile::Read(uint64_t offset, void *dst, size_t len) const
{
    if (mFile == nullptr || offset > mSize || len > mSize - offset) {
        return false;
    }
    if (fseek(mFile, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    return fread(dst, 1, len, mFile) == len;
}

/* ------------------------------------------------------------------ Atom */

Atom::Atom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : mFile(file), mOffset(offset), mSize(0), mType(0), mDataOffset(8), mDepth(depth), mValid(false)
{
    uint8_t hdr[16];
    if (offset + 8 > limit || !file.Read(offset, hdr, 8)) {
        return;
    }
    mSize = be32(hdr);
    mType = be32(hdr + 4);
    if (mSize == 1) {
        /* 64-bit size after the type */
        if (offset + 16 > limit || !file.Read(offset + 8, hdr + 8, 8)) {
            return;
        }
        mSize = be64(hdr + 8);
        mDataOffset = 16;
    } else if (mSize == 0) {
        /* runs to the end of its parent (of the file, at the top level) */
        mSize = limit - offset;
    }
    mValid = mSize >= mDataOffset && mSize <= limit - offset;
}

uint8_t Atom::readUint8(uint64_t at) const
{
    uint8_t v = 0;
    return readData(at, &v, 1) ? v : 0;
}

uint16_t Atom::readUint16(uint64_t at) const
{
    uint8_t b[2];
    return readData(at, b, 2) ? be16(b) : 0;
}

uint32_t Atom::readUint32(uint64_t at) const
{
    uint8_t b[4];
    return readData(at, b, 4) ? be32(b) : 0;
}

uint64_t Atom::readUint64(uint64_t at) const
{
    uint8_t b[8];
    return readData(at, b, 8) ? be64(b) : 0;
}

bool Atom::readData(uint64_t at, void *dst, size_t len) const
{
    return at + len <= mSize && mFile.Read(mOffset + at, dst, len);
}

std::vector<std::unique_ptr<Atom>> Atom::ParseAtoms(const QTFile &file, uint64_t start,
                                                    uint64_t end, size_t depth)
{
    std::vector<std::unique_ptr<Atom>> atoms;
    uint64_t pos = start;
    while (pos + 8 <= end) {
        uint8_t hdr[8];
        if (!file.Read(pos, hdr, sizeof(hdr))) {
            break;
        }
        std::unique_ptr<Atom> a;
        switch (be32(hdr + 4)) {
        case MovieAtom::TAG:                       a.reset(new MovieAtom(file, pos, end, depth)); break;
        case MovieHeaderAtom::TAG:                 a.reset(new MovieHeaderAtom(file, pos, end, depth)); break;
        case MovieDataAtom::TAG:                   a.reset(new MovieDataAtom(file, pos, end, depth)); break;
        case TrackAtom::TAG:                       a.reset(new TrackAtom(file, pos, end, depth)); break;
        case TrackHeaderAtom::TAG:                 a.reset(new TrackHeaderAtom(file, pos, end, depth)); break;
        case MediaAtom::TAG:                       a.reset(new MediaAtom(file, pos, end, depth)); break;
        case MediaHeaderAtom::TAG:                 a.reset(new MediaHeaderAtom(file, pos, end, depth)); break;
        case HandlerReferenceAtom::TAG:            a.reset(new HandlerReferenceAtom(file, pos, end, depth)); break;
        case MediaInformationAtom::TAG:            a.reset(new MediaInformationAtom(file, pos, end, depth)); break;
        case VideoMediaInformationHeaderAtom::TAG: a.reset(new VideoMediaInformationHeaderAtom(file, pos, end, depth)); break;
        case SampleTableAtom::TAG:                 a.reset(new SampleTableAtom(file, pos, end, depth)); break;
        case SampleDescriptionAtom::TAG:           a.reset(new SampleDescriptionAtom(file, pos, end, depth)); break;
        case TimeToSampleAtom::TAG:                a.reset(new TimeToSampleAtom(file, pos, end, depth)); break;
        case SampleToChunkAtom::TAG:               a.reset(new SampleToChunkAtom(file, pos, end, depth)); break;
        case SampleSizeAtom::TAG:                  a.reset(new SampleSizeAtom(file, pos, end, depth)); break;
        case ChunkOffsetAtom::TAG:
        case ChunkOffsetAtom::TAG64:               a.reset(new ChunkOffsetAtom(file, pos, end, depth)); break;
        default:                                   a.reset(new Atom(file, pos, end, depth)); break;
        }
        if (!a->IsValid() || a->GetSize() == 0) {
            break;   /* a size that runs past the parent: nothing after it can be trusted */
        }
        pos += a->GetSize();
        atoms.push_back(std::move(a));
    }
    return atoms;
}

/* ------------------------------------------------------------------ leaf atoms */

MovieHeaderAtom::MovieHeaderAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    const size_t d = mDataOffset;
    if (readUint8(d) == 1) {
        mTimeScale = readUint32(d + 20);
        mDuration = readUint64(d + 24);
    } else {
        mTimeScale = readUint32(d + 12);
        mDuration = readUint32(d + 16);
    }
}

TrackHeaderAtom::TrackHeaderAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    const size_t d = mDataOffset;
    mFlags = readUint32(d) & 0x00FFFFFF;
    /* version 1 widens creation, modification and duration to 64 bits: 12 bytes more */
    const size_t at = d + (readUint8(d) == 1 ? 88 : 76);
    mTrackWidth = readUint32(at);
    mTrackHeight = readUint32(at + 4);
}

MediaHeaderAtom::MediaHeaderAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    const size_t d = mDataOffset;
    if (readUint8(d) == 1) {
        mTimeScale = readUint32(d + 20);
        mDuration = readUint64(d + 24);
    } else {
        mTimeScale = readUint32(d + 12);
        mDuration = readUint32(d + 16);
    }
}

HandlerReferenceAtom::HandlerReferenceAtom(const QTFile &file, uint64_t offset, uint64_t limit,
                                           size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (mValid) {
        mComponentSubtype = readUint32(mDataOffset + 8);
    }
}

SampleDescriptionAtom::SampleDescriptionAtom(const QTFile &file, uint64_t offset, uint64_t limit,
                                             size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    /* version/flags, entry count, then the first entry: size, format, 6 reserved, data
     * reference index, version, revision, vendor, temporal and spatial quality, width, height */
    const size_t entry = mDataOffset + 8;
    if (readUint32(mDataOffset + 4) == 0 || readUint32(entry) < 36) {
        mValid = false;
        return;
    }
    mFormat = readUint32(entry + 4);
    mWidth = readUint16(entry + 32);
    mHeight = readUint16(entry + 34);
}

TimeToSampleAtom::TimeToSampleAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    std::vector<uint8_t> raw;
    const uint32_t count = mValid ? readUint32(mDataOffset + 4) : 0;
    if (!mValid || !read_table(mFile, mOffset, mSize, mDataOffset + 8, count, 8, raw)) {
        mValid = false;
        return;
    }
    mEntries.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        mEntries[i] = { be32(&raw[i * 8]), be32(&raw[i * 8 + 4]) };
    }
}

SampleToChunkAtom::SampleToChunkAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    std::vector<uint8_t> raw;
    const uint32_t count = mValid ? readUint32(mDataOffset + 4) : 0;
    if (!mValid || !read_table(mFile, mOffset, mSize, mDataOffset + 8, count, 12, raw)) {
        mValid = false;
        return;
    }
    mEntries.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        mEntries[i] = { be32(&raw[i * 12]), be32(&raw[i * 12 + 4]) };   /* + description index */
    }
}

SampleSizeAtom::SampleSizeAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    mSampleSize = readUint32(mDataOffset + 4);
    const uint32_t count = readUint32(mDataOffset + 8);
    if (count > MAX_ENTRIES) {
        mValid = false;
        return;
    }
    mNumberOfEntries = count;
    if (mSampleSize == 0) {
        /* the sketch read these one 4-byte seek at a time; one read is ~1800x fewer calls */
        std::vector<uint8_t> raw;
        if (!read_table(mFile, mOffset, mSize, mDataOffset + 12, count, 4, raw)) {
            mValid = false;
            return;
        }
        mSizes.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            mSizes[i] = be32(&raw[i * 4]);
        }
    }
}

ChunkOffsetAtom::ChunkOffsetAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    std::vector<uint8_t> raw;
    const size_t width = mType == TAG64 ? 8 : 4;
    const uint32_t count = mValid ? readUint32(mDataOffset + 4) : 0;
    if (!mValid || !read_table(mFile, mOffset, mSize, mDataOffset + 8, count, width, raw)) {
        mValid = false;
        return;
    }
    mOffsets.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        mOffsets[i] = width == 8 ? be64(&raw[i * 8]) : be32(&raw[i * 4]);
    }
}

/* ------------------------------------------------------------------ containers */

SampleTableAtom::SampleTableAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    for (auto &a : ParseChildren()) {
        switch (a->GetType()) {
        case SampleDescriptionAtom::TAG: mSampleDescription = take<SampleDescriptionAtom>(a); break;
        case TimeToSampleAtom::TAG:      mTimeToSample = take<TimeToSampleAtom>(a); break;
        case SampleToChunkAtom::TAG:     mSampleToChunk = take<SampleToChunkAtom>(a); break;
        case SampleSizeAtom::TAG:        mSampleSize = take<SampleSizeAtom>(a); break;
        case ChunkOffsetAtom::TAG:
        case ChunkOffsetAtom::TAG64:     mChunkOffset = take<ChunkOffsetAtom>(a); break;
        default: break;   /* ctts, stss, sdtp, ...: not needed for all-keyframe MJPEG */
        }
    }
}

MediaInformationAtom::MediaInformationAtom(const QTFile &file, uint64_t offset, uint64_t limit,
                                           size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    for (auto &a : ParseChildren()) {
        switch (a->GetType()) {
        case VideoMediaInformationHeaderAtom::TAG:
            mVideoMediaInformationHeader = take<VideoMediaInformationHeaderAtom>(a);
            break;
        case SampleTableAtom::TAG:
            mSampleTable = take<SampleTableAtom>(a);
            break;
        default:
            break;
        }
    }
}

MediaAtom::MediaAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    for (auto &a : ParseChildren()) {
        switch (a->GetType()) {
        case MediaHeaderAtom::TAG:      mMediaHeader = take<MediaHeaderAtom>(a); break;
        case HandlerReferenceAtom::TAG: mHandlerReference = take<HandlerReferenceAtom>(a); break;
        case MediaInformationAtom::TAG: mMediaInformation = take<MediaInformationAtom>(a); break;
        default: break;
        }
    }
}

TrackAtom::TrackAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    for (auto &a : ParseChildren()) {
        switch (a->GetType()) {
        case TrackHeaderAtom::TAG: mTrackHeader = take<TrackHeaderAtom>(a); break;
        case MediaAtom::TAG:       mMedia = take<MediaAtom>(a); break;
        default: break;
        }
    }
}

bool TrackAtom::IsVideoTrack()
{
    if (mMedia == nullptr || mMedia->GetMediaInformation() == nullptr) {
        return false;
    }
    HandlerReferenceAtom *hdlr = mMedia->GetHandlerReference();
    return mMedia->GetMediaInformation()->IsVideoTrack() ||
           (hdlr != nullptr && hdlr->ComponentSubtype() == FourCC('v', 'i', 'd', 'e'));
}

MovieAtom::MovieAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth)
    : Atom(file, offset, limit, depth)
{
    if (!mValid) {
        return;
    }
    for (auto &a : ParseChildren()) {
        if (a->GetType() == MovieHeaderAtom::TAG) {
            mMovieHeader = take<MovieHeaderAtom>(a);
        } else if (a->GetType() == TrackAtom::TAG && mVideoTrack == nullptr) {
            std::unique_ptr<TrackAtom> track = take<TrackAtom>(a);
            if (track->IsVideoTrack()) {
                mVideoTrack = std::move(track);
            }
        }
    }
}

/* ------------------------------------------------------------------ QuickTimeFile */

QuickTimeFile::QuickTimeFile(FILE *file) : mFile(file)
{
    if (mFile.Size() == 0) {
        mError = "empty or unreadable file";
        return;
    }
    std::unique_ptr<MovieAtom> movie;
    for (auto &a : Atom::ParseAtoms(mFile, 0, mFile.Size(), 0)) {
        if (a->GetType() == MovieAtom::TAG && movie == nullptr) {
            mMoovOffset = a->GetOffset();
            movie = take<MovieAtom>(a);
        } else if (a->GetType() == MovieDataAtom::TAG && mMdatOffset == 0) {
            mMdatOffset = a->GetOffset();
        }
    }
    if (movie == nullptr || !movie->IsValid()) {
        mError = "no movie atom (moov) -- not a QuickTime file, or cut short";
        return;
    }
    TrackAtom *track = movie->GetVideoTrack();
    MediaAtom *media = track ? track->GetMedia() : nullptr;
    SampleTableAtom *stbl = media && media->GetMediaInformation()
                          ? media->GetMediaInformation()->GetSampleTable() : nullptr;
    if (stbl == nullptr) {
        mError = "no video track with a sample table";
        return;
    }
    SampleSizeAtom *stsz = stbl->GetSampleSize();
    SampleToChunkAtom *stsc = stbl->GetSampleToChunk();
    ChunkOffsetAtom *stco = stbl->GetChunkOffset();
    if (stsz == nullptr || !stsz->IsValid() || stsc == nullptr || !stsc->IsValid() ||
        stco == nullptr || !stco->IsValid() || stsc->Entries().empty()) {
        mError = "sample table incomplete (stsz, stsc or stco missing or damaged)";
        return;
    }

    SampleDescriptionAtom *stsd = stbl->GetSampleDescription();
    mCodec = stsd && stsd->IsValid() ? stsd->Format() : 0;
    TrackHeaderAtom *tkhd = track->GetTrackHeader();
    mWidth = tkhd ? tkhd->TrackWidth() : 0;
    mHeight = tkhd ? tkhd->TrackHeight() : 0;
    if ((mWidth == 0 || mHeight == 0) && stsd != nullptr) {
        mWidth = stsd->Width();
        mHeight = stsd->Height();
    }

    MediaHeaderAtom *mdhd = media->GetMediaHeader();
    mTimeScale = mdhd && mdhd->TimeScale() ? mdhd->TimeScale()
               : (movie->GetMovieHeader() ? movie->GetMovieHeader()->TimeScale() : 0);
    if (stbl->GetTimeToSample() != nullptr && stbl->GetTimeToSample()->IsValid()) {
        mDeltas = stbl->GetTimeToSample()->Entries();
    }
    if (mTimeScale == 0 || mDeltas.empty()) {
        /* no usable timing: the sketch's fixed 30 fps */
        mTimeScale = 30;
        mDeltas = { { UINT32_MAX, 1 } };
    }

    /*
     * Where each frame is. Chunks are runs of consecutive frames at an offset (stco); stsc
     * says how many frames each chunk holds, as runs starting at a chunk number. ffmpeg
     * writes one chunk per MiB, so even these small clips have two -- and the sketch's
     * "frames run back to back from the start of mdat" only held because nothing else was
     * interleaved.
     */
    const size_t frames = stsz->GetNumberOfEntries();
    const std::vector<uint64_t> &chunks = stco->Offsets();
    const std::vector<SampleToChunkAtom::Entry> &runs = stsc->Entries();
    mChunks = chunks.size();
    mSizes.resize(frames);
    mOffsets.resize(frames);
    size_t frame = 0, run = 0;
    for (size_t c = 0; c < chunks.size() && frame < frames; c++) {
        while (run + 1 < runs.size() && runs[run + 1].firstChunk <= c + 1) {
            run++;
        }
        uint64_t at = chunks[c];
        for (uint32_t k = 0; k < runs[run].samplesPerChunk && frame < frames; k++, frame++) {
            const size_t size = stsz->GetFrameSize(frame);
            if (size == 0 || at + size > mFile.Size() || at > UINT32_MAX) {
                mError = "a frame lies outside the file";
                return;
            }
            mSizes[frame] = (uint32_t)size;
            mOffsets[frame] = (uint32_t)at;
            mTotalBytes += size;
            mMaxSize = size > mMaxSize ? size : mMaxSize;
            mMinSize = (mMinSize == 0 || size < mMinSize) ? size : mMinSize;
            at += size;
        }
    }
    if (frame < frames) {
        mError = "the chunk tables place fewer frames than the size table lists";
        return;
    }
}

ssize_t QuickTimeFile::GetFrameSize(size_t idx) const
{
    if (!IsValid()) {
        return -1;
    }
    return idx < mSizes.size() ? (ssize_t)mSizes[idx] : -2;
}

ssize_t QuickTimeFile::GetFrame(size_t idx, uint8_t *dataOut, size_t dataOutSize) const
{
    if (!IsValid() || idx >= mSizes.size()) {
        return -1;
    }
    if (mSizes[idx] > dataOutSize) {
        return -2;
    }
    return mFile.Read(mOffsets[idx], dataOut, mSizes[idx]) ? (ssize_t)mSizes[idx] : -1;
}

uint32_t QuickTimeFile::FrameDelta(size_t idx) const
{
    /* runs of equal durations: usually one for a whole clip */
    size_t first = 0;
    for (const auto &r : mDeltas) {
        if (idx - first < r.count) {
            return r.delta;
        }
        first += r.count;
    }
    return mDeltas.back().delta;
}

uint64_t QuickTimeFile::DurationUs() const
{
    uint64_t ticks = 0;
    size_t left = mSizes.size();
    for (const auto &r : mDeltas) {
        const size_t n = r.count < left ? r.count : left;
        ticks += (uint64_t)n * r.delta;
        left -= n;
    }
    ticks += (uint64_t)left * mDeltas.back().delta;
    return mTimeScale ? ticks * 1000000ULL / mTimeScale : 0;
}

void QuickTimeFile::Describe(const char *name) const
{
    if (!IsValid()) {
        printf("%s: %s\n", name, Error());
        return;
    }
    char codec[5];
    fourcc(mCodec, codec);
    const double secs = DurationUs() / 1e6;
    const size_t frames = mSizes.size();
    printf("%s: %" PRIu32 "x%" PRIu32 " '%s', %u frames, %.2f s, %.2f fps (timescale %" PRIu32 ")\n",
           name, mWidth, mHeight, codec, (unsigned)frames, secs, secs > 0 ? frames / secs : 0.0,
           mTimeScale);
    printf("frames %u..%u bytes (avg %u); %u chunk%s; mdat at %" PRIu64 ", moov at %" PRIu64 "%s\n",
           (unsigned)mMinSize, (unsigned)mMaxSize, frames ? (unsigned)(mTotalBytes / frames) : 0,
           (unsigned)mChunks, mChunks == 1 ? "" : "s", mMdatOffset, mMoovOffset,
           mMoovOffset > mMdatOffset ? " (index at the end)" : "");
}

}  // namespace quicktime
