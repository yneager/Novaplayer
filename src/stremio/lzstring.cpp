#include "stremio/lzstring.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace stremio {

namespace {

class BitWriter
{
public:
    BitWriter(int bitsPerChar, const char *alphabet)
        : bitsPerChar_(bitsPerChar)
        , alphabet_(alphabet)
    {
    }

    // Writes `count` bits of `value`, least significant bit first.
    void writeValueLsbFirst(int value, int count)
    {
        for (int i = 0; i < count; ++i) {
            pushBit(value & 1);
            value >>= 1;
        }
    }

    // The "1 followed by zeros" / "all zeros" markers of _compress.ts shift
    // a constant into the buffer without masking.
    void writeMarker(int firstBit, int count)
    {
        int value = firstBit;
        for (int i = 0; i < count; ++i) {
            pushBit(value);
            value = 0;
        }
    }

    void flush()
    {
        for (;;) {
            data_ <<= 1;
            if (position_ == bitsPerChar_ - 1) {
                out_.append(QLatin1Char(alphabet_[data_]));
                break;
            }
            ++position_;
        }
    }

    QString result() const { return out_; }

private:
    void pushBit(int bit)
    {
        data_ = (data_ << 1) | bit;
        if (position_ == bitsPerChar_ - 1) {
            position_ = 0;
            out_.append(QLatin1Char(alphabet_[data_]));
            data_ = 0;
        } else {
            ++position_;
        }
    }

    int bitsPerChar_;
    const char *alphabet_;
    int data_ = 0;
    int position_ = 0;
    QString out_;
};

} // namespace

QString lzCompressToEncodedUriComponent(const QString &input)
{
    static const char kKeyStrUriSafe[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-$";
    BitWriter writer(6, kKeyStrUriSafe);

    QHash<QString, int> dictionary;
    QSet<QString> dictionaryToCreate;
    QString w;
    int enlargeIn = 2; // compensate for the first entry which should not count
    int dictSize = 3;
    int numBits = 2;

    auto enlarge = [&] {
        --enlargeIn;
        if (enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
    };

    auto emitW = [&] {
        if (dictionaryToCreate.contains(w)) {
            const int code = w.at(0).unicode();
            if (code < 256) {
                writer.writeMarker(0, numBits);
                writer.writeValueLsbFirst(code, 8);
            } else {
                writer.writeMarker(1, numBits);
                writer.writeValueLsbFirst(code, 16);
            }
            enlarge();
            dictionaryToCreate.remove(w);
        } else {
            writer.writeValueLsbFirst(dictionary.value(w), numBits);
        }
        enlarge();
    };

    for (qsizetype i = 0; i < input.size(); ++i) {
        const QString c(input.at(i));
        if (!dictionary.contains(c)) {
            dictionary.insert(c, dictSize++);
            dictionaryToCreate.insert(c);
        }
        const QString wc = w + c;
        if (dictionary.contains(wc)) {
            w = wc;
        } else {
            emitW();
            dictionary.insert(wc, dictSize++);
            w = c;
        }
    }
    if (!w.isEmpty()) {
        emitW();
    }
    // End of stream marker.
    writer.writeValueLsbFirst(2, numBits);
    writer.flush();
    return writer.result();
}

} // namespace stremio
