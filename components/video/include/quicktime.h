/*
 * QuickTime (.mov) parsing for MJPEG clips, ported from the Flash_PNG sketch's
 * QuickTimeFile.h/.cpp. The atom classes and QuickTimeFile's interface -- Width(), Height(),
 * FrameCount(), GetFrameSize(), GetFrame() -- are the sketch's. What changed:
 *
 *   - I/O goes through QTFile, a thin wrapper on a stdio FILE, instead of Arduino's fs::File,
 *     so it reads from any VFS volume;
 *   - frames are located through the chunk tables (stsc + stco/co64) instead of being assumed
 *     to run back to back from the start of mdat, so any frame can be read directly (the
 *     sketch's random access read from offset 0);
 *   - frame durations come from stts and the media timescale (mdhd), for playback timing;
 *   - the sample description (stsd) is read, so the codec can be checked;
 *   - 64-bit and to-end-of-parent atom sizes are handled, and every size and table length
 *     is checked against the file, so a damaged file fails to open instead of misbehaving;
 *   - the iostream dump (compiled out under Arduino anyway) is replaced by Describe().
 *
 * The atom tree is only needed while opening: QuickTimeFile keeps the frame tables and
 * drops the rest.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <memory>
#include <vector>

namespace quicktime {

constexpr uint32_t FourCC(char a, char b, char c, char d)
{
    return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
           (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
}

/* A seekable file: all the parser needs from its storage. */
class QTFile {
public:
    explicit QTFile(FILE *file);
    uint64_t Size() const { return mSize; }
    /* Exactly `len` bytes at `offset`, or false. */
    bool Read(uint64_t offset, void *dst, size_t len) const;

private:
    FILE *mFile;
    uint64_t mSize;
};

class Atom {
public:
    Atom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    virtual ~Atom() = default;

    uint64_t GetOffset() const { return mOffset; }
    uint64_t GetSize() const { return mSize; }
    uint32_t GetType() const { return mType; }
    size_t GetDepth() const { return mDepth; }
    /* False when the atom runs past its parent or its contents do not add up. */
    bool IsValid() const { return mValid; }

    /* The atoms in [start, end), each built as the class for its type. Stops at the first
     * one that does not fit. */
    static std::vector<std::unique_ptr<Atom>> ParseAtoms(const QTFile &file, uint64_t start,
                                                         uint64_t end, size_t depth);

protected:
    const QTFile &mFile;
    uint64_t mOffset;
    uint64_t mSize;
    uint32_t mType;
    size_t mDataOffset;   /* header length: 8, or 16 with a 64-bit size */
    size_t mDepth;
    bool mValid;

    /* Big-endian reads at an offset from the start of the atom; 0 past its end. */
    uint8_t readUint8(uint64_t at) const;
    uint16_t readUint16(uint64_t at) const;
    uint32_t readUint32(uint64_t at) const;
    uint64_t readUint64(uint64_t at) const;
    bool readData(uint64_t at, void *dst, size_t len) const;

    uint64_t BodySize() const { return mSize - mDataOffset; }
    std::vector<std::unique_ptr<Atom>> ParseChildren() const
    {
        return ParseAtoms(mFile, mOffset + mDataOffset, mOffset + mSize, mDepth + 1);
    }
};

class MovieHeaderAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('m', 'v', 'h', 'd');
    MovieHeaderAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    uint32_t TimeScale() const { return mTimeScale; }
    uint64_t Duration() const { return mDuration; }

private:
    uint32_t mTimeScale = 0;
    uint64_t mDuration = 0;
};

class TrackHeaderAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('t', 'k', 'h', 'd');
    TrackHeaderAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    uint32_t TrackWidth() const { return mTrackWidth >> 16; }    /* 16.16 fixed point */
    uint32_t TrackHeight() const { return mTrackHeight >> 16; }
    bool IsEnabled() const { return (mFlags & 0x0001) != 0; }

private:
    uint32_t mFlags = 0;
    uint32_t mTrackWidth = 0;
    uint32_t mTrackHeight = 0;
};

class MediaHeaderAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('m', 'd', 'h', 'd');
    MediaHeaderAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    uint32_t TimeScale() const { return mTimeScale; }
    uint64_t Duration() const { return mDuration; }

private:
    uint32_t mTimeScale = 0;
    uint64_t mDuration = 0;
};

class HandlerReferenceAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('h', 'd', 'l', 'r');
    HandlerReferenceAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    uint32_t ComponentSubtype() const { return mComponentSubtype; }   /* 'vide', 'soun', ... */

private:
    uint32_t mComponentSubtype = 0;
};

class VideoMediaInformationHeaderAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('v', 'm', 'h', 'd');
    using Atom::Atom;
};

class SampleDescriptionAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('s', 't', 's', 'd');
    SampleDescriptionAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    uint32_t Format() const { return mFormat; }   /* codec fourcc: 'jpeg' for ffmpeg's MJPEG */
    uint16_t Width() const { return mWidth; }
    uint16_t Height() const { return mHeight; }

private:
    uint32_t mFormat = 0;
    uint16_t mWidth = 0;
    uint16_t mHeight = 0;
};

class TimeToSampleAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('s', 't', 't', 's');
    struct Entry {
        uint32_t count;
        uint32_t delta;   /* media timescale units per sample */
    };
    TimeToSampleAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    const std::vector<Entry> &Entries() const { return mEntries; }

private:
    std::vector<Entry> mEntries;
};

class SampleToChunkAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('s', 't', 's', 'c');
    struct Entry {
        uint32_t firstChunk;        /* 1-based */
        uint32_t samplesPerChunk;
    };
    SampleToChunkAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    const std::vector<Entry> &Entries() const { return mEntries; }

private:
    std::vector<Entry> mEntries;
};

class SampleSizeAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('s', 't', 's', 'z');
    SampleSizeAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    size_t GetNumberOfEntries() const { return mNumberOfEntries; }
    size_t GetFrameSize(size_t idx) const
    {
        return mSampleSize ? mSampleSize : (idx < mSizes.size() ? mSizes[idx] : 0);
    }

private:
    uint32_t mSampleSize = 0;   /* non-zero: every sample is this size and there is no table */
    size_t mNumberOfEntries = 0;
    std::vector<uint32_t> mSizes;
};

/* 'stco' (32-bit offsets) or 'co64'. */
class ChunkOffsetAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('s', 't', 'c', 'o');
    static constexpr uint32_t TAG64 = FourCC('c', 'o', '6', '4');
    ChunkOffsetAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    const std::vector<uint64_t> &Offsets() const { return mOffsets; }

private:
    std::vector<uint64_t> mOffsets;
};

class SampleTableAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('s', 't', 'b', 'l');
    SampleTableAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    SampleDescriptionAtom *GetSampleDescription() { return mSampleDescription.get(); }
    TimeToSampleAtom *GetTimeToSample() { return mTimeToSample.get(); }
    SampleToChunkAtom *GetSampleToChunk() { return mSampleToChunk.get(); }
    SampleSizeAtom *GetSampleSize() { return mSampleSize.get(); }
    ChunkOffsetAtom *GetChunkOffset() { return mChunkOffset.get(); }

private:
    std::unique_ptr<SampleDescriptionAtom> mSampleDescription;
    std::unique_ptr<TimeToSampleAtom> mTimeToSample;
    std::unique_ptr<SampleToChunkAtom> mSampleToChunk;
    std::unique_ptr<SampleSizeAtom> mSampleSize;
    std::unique_ptr<ChunkOffsetAtom> mChunkOffset;
};

class MediaInformationAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('m', 'i', 'n', 'f');
    MediaInformationAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    bool IsVideoTrack() const { return mVideoMediaInformationHeader != nullptr; }
    SampleTableAtom *GetSampleTable() { return mSampleTable.get(); }

private:
    std::unique_ptr<VideoMediaInformationHeaderAtom> mVideoMediaInformationHeader;
    std::unique_ptr<SampleTableAtom> mSampleTable;
};

class MediaAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('m', 'd', 'i', 'a');
    MediaAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    MediaHeaderAtom *GetMediaHeader() { return mMediaHeader.get(); }
    HandlerReferenceAtom *GetHandlerReference() { return mHandlerReference.get(); }
    MediaInformationAtom *GetMediaInformation() { return mMediaInformation.get(); }

private:
    std::unique_ptr<MediaHeaderAtom> mMediaHeader;
    std::unique_ptr<HandlerReferenceAtom> mHandlerReference;
    std::unique_ptr<MediaInformationAtom> mMediaInformation;
};

class TrackAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('t', 'r', 'a', 'k');
    TrackAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    TrackHeaderAtom *GetTrackHeader() { return mTrackHeader.get(); }
    MediaAtom *GetMedia() { return mMedia.get(); }
    bool IsVideoTrack();

private:
    std::unique_ptr<TrackHeaderAtom> mTrackHeader;
    std::unique_ptr<MediaAtom> mMedia;
};

class MovieAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('m', 'o', 'o', 'v');
    MovieAtom(const QTFile &file, uint64_t offset, uint64_t limit, size_t depth);
    MovieHeaderAtom *GetMovieHeader() { return mMovieHeader.get(); }
    TrackAtom *GetVideoTrack() { return mVideoTrack.get(); }   /* the first one */

private:
    std::unique_ptr<MovieHeaderAtom> mMovieHeader;
    std::unique_ptr<TrackAtom> mVideoTrack;
};

class MovieDataAtom : public Atom {
public:
    static constexpr uint32_t TAG = FourCC('m', 'd', 'a', 't');
    using Atom::Atom;
};

class QuickTimeFile {
public:
    /* Parses the file's structure; the FILE must stay open for GetFrame(). */
    explicit QuickTimeFile(FILE *file);

    bool IsValid() const { return mError == nullptr; }
    const char *Error() const { return mError ? mError : ""; }

    // Get the resolutions
    uint32_t Width() const { return mWidth; }
    uint32_t Height() const { return mHeight; }
    uint32_t Codec() const { return mCodec; }

    // get the number of frames
    ssize_t FrameCount() const { return IsValid() ? (ssize_t)mSizes.size() : -1; }

    ssize_t GetFrameSize(size_t idx) const;
    /* Frame `idx`'s bytes into dataOut: its size, -1 if unreadable, -2 if dataOut is small. */
    ssize_t GetFrame(size_t idx, uint8_t *dataOut, size_t dataOutSize) const;
    size_t MaxFrameSize() const { return mMaxSize; }

    /* Timing: frame `idx` lasts FrameDelta(idx) / TimeScale() seconds. */
    uint32_t TimeScale() const { return mTimeScale; }
    uint32_t FrameDelta(size_t idx) const;
    uint64_t DurationUs() const;

    /* One-paragraph summary, printf'd. */
    void Describe(const char *name) const;

private:
    QTFile mFile;
    const char *mError = nullptr;
    uint32_t mWidth = 0, mHeight = 0, mCodec = 0, mTimeScale = 0;
    size_t mMaxSize = 0, mMinSize = 0, mChunks = 0;
    uint64_t mTotalBytes = 0, mMdatOffset = 0, mMoovOffset = 0;
    std::vector<uint32_t> mSizes;
    std::vector<uint32_t> mOffsets;   /* a 16 MB flash holds nothing past 4 GB */
    std::vector<TimeToSampleAtom::Entry> mDeltas;
};

}  // namespace quicktime
