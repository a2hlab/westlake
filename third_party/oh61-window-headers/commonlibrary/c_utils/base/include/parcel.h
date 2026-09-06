/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 /**
  * @file parcel.h
  *
  * @brief Provides classes for the data container implemented in c_utils.
  *
  * The <b>Parcel</b> and <b>Parcelable</b> classes and the related memory
  * allocator are provided.
  */

#ifndef OHOS_UTILS_PARCEL_H
#define OHOS_UTILS_PARCEL_H

#include <string>
#include <vector>
#include "nocopyable.h"
#include "refbase.h"
#include "flat_obj.h"

namespace OHOS {

class Parcel;

/**
 * @brief Defines a class for which the instance can be written into a parcel.
 *
 * @note If this object is remote, its position will be used in
 * kernel data transaction.
 */
class Parcelable : public virtual RefBase {
public:
    virtual ~Parcelable() = default;

    Parcelable();
    /**
     * @brief Creates a `Parcelable` object.
     *
     * @param asRemote Specifies whether the object is remote.
     */
    explicit Parcelable(bool asRemote);

    /**
     * @brief Writes a `Parcelable` object into a parcel.
     *
     * @param parcel Indicates the parcel.
     * @return Returns `true` if the operation is successful; returns `false`
     * otherwise.
     * @note If the `Parcelable` object is remote, its position will be saved
     * in the parcel.
     * @note You must implement a static Unmarshalling function to
     * fetch data from the given parcel into this `Parcelable` object.
     * See `static TestParcelable *Unmarshalling(Parcel &parcel)` as an example.
     */
    virtual bool Marshalling(Parcel &parcel) const = 0;

    /**
     * @brief Enumerates the behavior types of a `Parcelable` object.
     *
     * @var IPC Indicate an object that can be used in IPC.
     * @var RPC Indicate an object that can be used in RPC.
     * @var HOLD_OBJECT Indicate an object that will be always alive
     * during data transaction.
     *
     */
    enum BehaviorFlag { IPC = 0x01, RPC = 0x02, HOLD_OBJECT = 0x10 };

    /**
     * @brief Enables the specified behavior.
     *
     * @param b Indicates the behavior.
     * @see BehaviorFlag.
     */
    inline void SetBehavior(BehaviorFlag b) const
    {
        behavior_ |= static_cast<uint8_t>(b);
    }

    /**
     * @brief Disables the specified behavior.
     *
     * @param b Indicates the behavior.
     * @see BehaviorFlag.
     */
    inline void ClearBehavior(BehaviorFlag b) const
    {
        behavior_ &= static_cast<uint8_t>(~b);
    }

    /**
     * @brief Checks whether the specified behavior is enabled.
     *
     * @param b Indicates the behavior.

     * @return Returns `true` if the behavior is enabled; returns `false`
     * otherwise.
     * @see BehaviorFlag.
     */
    inline bool TestBehavior(BehaviorFlag b) const
    {
        return behavior_ & (static_cast<uint8_t>(b));
    }

public:
    bool asRemote_; // If the object is remote.
    mutable uint8_t behavior_; // Behavior of the object.
};

/**
 * @brief Defines a memory allocator for data in `Parcel`.
 */
class Allocator {
public:
    virtual ~Allocator() = default;

    virtual void *Realloc(void *data, size_t newSize) = 0;

    virtual void *Alloc(size_t size) = 0;

    virtual void Dealloc(void *data) = 0;
};

/**
 * @brief Provides the default implementation for a memory allocator.
 *
 * @note A non-default allocator for a parcel must be specified manually.
 */
class DefaultAllocator : public Allocator {
public:
    /**
     * @brief Allocates memory for this parcel.
     *
     * @param size Indicates the size of the memory to allocate.
     * @return Returns the void pointer to the memory region.
     */
    void *Alloc(size_t size) override;

    /**
     * @brief Deallocates memory for this parcel.
     *
     * @param data Indicates the void pointer to the memory region.
     */
    void Dealloc(void *data) override;
private:
    /**
     * @brief Reallocates memory for this parcel.
     *
     * @param data Indicates the void pointer to the existing memory region.
     * @param newSize Indicates the size of the memory to reallocate.
     * @return Returns the void pointer to the new memory region.
     */
    void *Realloc(void *data, size_t newSize) override;
};

/**
 * @brief Provides a data/message container.
 *
 * This class provides methods for writing and reading data of various types,
 * including primitives and parcelable objects.
 *
 * @note This class is usually used in IPC and RPC scenarios.
 */
class Parcel {
public:
    Parcel();

    /**
     * @brief Creates a `Parcel` object with the specified memory allocator.
     *
     * @param allocator Indicates the memory allocator.
     */
    explicit Parcel(Allocator *allocator);

    virtual ~Parcel();

    /**
     * @brief Obtains the total size of existing data in this parcel.
     *
     * @return Returns the size, in bytes.
     */
    size_t GetDataSize() const;

    /**
     * @brief Obtains the pointer to the beginning of data in this parcel.
     *
     * @return Returns a pointer of the `uintptr_t` type.
     */
    uintptr_t GetData() const;

    /**
     * @brief Obtains the position (offset) of every object written
     * in this parcel.
     *
     * @return Returns a pointer of the `binder_size_t` type to
     * the first slot of the position array.
     * @see flat_obj.h
     */
    binder_size_t GetObjectOffsets() const;

    /**
     * @brief Obtains the size of the position array.
     *
     * @return Returns the size, in bytes.
     */
    size_t GetOffsetsSize() const;

    /**
     * @brief Obtains the total number of available bytes to write
     * into this parcel.
     *
     * @return Returns the number of available bytes.
     */
    size_t GetWritableBytes() const;

    /**
     * @brief Obtains the total number of available bytes to read
     * from this parcel.
     *
     * @return Returns the number of available bytes.
     */
    size_t GetReadableBytes() const;

    /**
     * @brief Obtains the total capacity of this parcel, that is, the size of
     * the current data region in the parcel.
     *
     * @return Returns the capacity, in bytes.
     */
    size_t GetDataCapacity() const;

    /**
     * @brief Obtains the maximum capacity of this parcel.
     *
     * @return Returns the capacity, in bytes.
     */
    size_t GetMaxCapacity() const;

    /**
     * @brief Sets the capacity for this parcel, that is, the size of the
     * current data region in the parcel.
     *
     * @param newCapacity Indicates the capacity to set.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     * @note The memory allocator will try to reallocate the data region
     * with the new capacity.
     *
     */
    bool SetDataCapacity(size_t newCapacity);

    /**
     * @brief Sets the total size of existing data in this parcel.
     *
     * @param dataSize Indicates the size, in bytes.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     * @note Do not call this function independently; otherwise, it may fail to
     * return the correct data size.
     */
    bool SetDataSize(size_t dataSize);

    /**
     * @brief Sets the maximum capacity for this parcel.
     *
     * @param maxCapacity Indicates the maximum capacity to set.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool SetMaxCapacity(size_t maxCapacity);

    // write primitives in alignment
    bool WriteBool(bool value);
    bool WriteInt8(int8_t value);
    bool WriteInt16(int16_t value);
    bool WriteInt32(int32_t value);
    bool WriteInt64(int64_t value);
    bool WriteUint8(uint8_t value);
    bool WriteUint16(uint16_t value);
    bool WriteUint32(uint32_t value);
    bool WriteUint64(uint64_t value);
    bool WriteFloat(float value);
    bool WriteDouble(double value);
    bool WritePointer(uintptr_t value);

    /**
     * @brief Writes a data region (buffer) to this parcel.
     *
     * @param data Indicates the void pointer to the buffer.
     * @param size Indicates the size of the buffer.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteBuffer(const void *data, size_t size);

    /**
     * @brief Writes a data region (buffer) to this parcel in alignment
     * and with the terminator replaced.
     *
     * @param data Indicates the void pointer to the buffer.
     * @param size Indicates the size of the buffer.
     * @param typeSize Indicates the size of the terminator.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     * @note The last several bytes specified by `typeSize` of the aligned data
     * will be treated as a terminator and replaced by '0b00000000'.
     */
    bool WriteBufferAddTerminator(const void *data, size_t size, size_t typeSize);

    /**
     * @brief Writes a data region (buffer) to this parcel.
     *
     * Currently, this function provides the same capability as `WriteBuffer()`.
     *
     * @param data Indicates the void pointer to the buffer.
     * @param size Indicates the size of the buffer.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteUnpadBuffer(const void *data, size_t size);

    /**
     * @brief Writes a C-style string to this parcel.
     *
     * The default terminator `\0` of the C-style string will also be written.
     *
     * @param value Indicates a pointer of the char type to a C-style string.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteCString(const char *value);

    /**
     * @brief Writes a C++ string (`std::string`) to this parcel.
     *
     * The exact length of the string will be written first, and then the string
     * itself with the appended terminator `\0` will be written.
     *
     * @param value Indicates the reference to an `std::string` object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteString(const std::string &value);

    /**
     * @brief Writes a C++ UTF-16 encoded string (`std::u16string`)
     * to this parcel.
     *
     * The exact length of the string will be written first, and then the string
     * itself with the appended terminator `\0` will be written.
     *
     * @param value Indicates the reference to an `std::u16string` object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteString16(const std::u16string &value);

    /**
     * @brief Writes a UTF-16 encoded string with the specified length
     * to this parcel.
     *
     * An `std::u16string` object will be constructed based on the `char16_t*`
     * pointer and the length `len` first. Then the input length and the string
     * data in the `u16string` object with the appended terminator `\0` will
     * be written.
     *
     * @param value Indicates the pointer to a UTF-16 encoded string.
     * @param len Indicates the exact length of the input string.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteString16WithLength(const char16_t *value, size_t len);

    /**
     * @brief Writes a UTF-8 encoded string with the specified length
     * to this parcel.
     *
     * The input length `len` and the string itself
     * with the appended terminator `\0` will be written.
     *
     * @param value Indicates the pointer to a UTF-8 encoded string.
     * @param len Indicates the exact length of the input string.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteString8WithLength(const char *value, size_t len);

    /**
     * @brief Writes a `Parcelable` object to this parcel.
     *
     * Call `WriteRemoteObject(const Parcelable *)` to write a remote object.
     * Call `Marshalling(Parcel &parcel)` to write a non-remote object.
     *
     * @param object Indicates the pointer to a `Parcelable` object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     * @note The value '0' of `Int32_t` will be written if a null pointer
     * is passed in.
     */
    bool WriteParcelable(const Parcelable *object);

    /**
     * @brief Writes a `Parcelable` object to this parcel, and enables its
     * behavior of `HOLD_OBJECT`.
     *
     * @param object Indicates the smart pointer to a `Parcelable` object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteStrongParcelable(const sptr<Parcelable> &object);

    /**
     * @brief Writes a remote object to this parcel.
     *
     * @param object Indicates the pointer to a remote object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     * @note If `HOLD_OBJECT` is enabled for the remote object, it will stay
     * alive as long as this parcel is alive.
     *
     */
    bool WriteRemoteObject(const Parcelable *object);

    /**
     * @brief Writes an object to this parcel.
     *
     * Use its own `Marshalling(Parcel &parcel)` when a null pointer is passed
     * in; in other scenarios, use `WriteRemoteObject(const Parcelable *)`.
     *
     * @tparam T Indicates the class type of the object.
     * @param object Indicates the smart pointer to the object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    template<typename T>
    bool WriteObject(const sptr<T> &object);

    /**
     * @brief Parses input data by this parcel.
     *
     * @param data Indicates the pointer to input data.
     * @param size Indicates the size of the input data, in bytes.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     * @note Only the read operation from this parcel is allowed after
     * successful calling of this method.
     */
    bool ParseFrom(uintptr_t data, size_t size);

    bool ReadBool();

    int8_t ReadInt8();

    int16_t ReadInt16();

    int32_t ReadInt32();

    int64_t ReadInt64();

    uint8_t ReadUint8();

    uint16_t ReadUint16();

    uint32_t ReadUint32();

    uint64_t ReadUint64();

    float ReadFloat();

    double ReadDouble();

    uintptr_t ReadPointer();

    bool ReadBool(bool &value);

    bool ReadInt8(int8_t &value);

    bool ReadInt16(int16_t &value);

    bool ReadInt32(int32_t &value);

    bool ReadInt64(int64_t &value);

    bool ReadUint8(uint8_t &value);

    bool ReadUint16(uint16_t &value);

    bool ReadUint32(uint32_t &value);

    bool ReadUint64(uint64_t &value);

    bool ReadFloat(float &value);

    bool ReadDouble(double &value);

    /**
     * @brief Reads a block of data (buffer data) from this parcel.
     *
     * @param length Indicates the size of the buffer, in bytes.
     * @return Returns a pointer of the `uint8_t` type to the buffer.
     */
    const uint8_t *ReadBuffer(size_t length);

    /**
     * @brief Read a block of data (buffer data) from this parcel.
     *
     * @param length Size of the buffer(Bytes).
     * @return A `uint8_t` pointer to the buffer.
     */
    const uint8_t *ReadBuffer(size_t length, bool isValidate);

    /**
     * @brief Reads a block of data (buffer data) without padding (alignment)
     * from this parcel.
     *
     * This method will read the effective data with the specified
     * `length` and discard the bytes used for padding.
     *
     * @param length Indicates the effective size of the buffer, in bytes.
     * @return Returns a pointer of the `uint8_t` type to the buffer.
     *
     */
    const uint8_t *ReadUnpadBuffer(size_t length);

    /**
     * @brief Skips the next several bytes specified by `bytes` in the read
     * operation.
     *
     * @param bytes Indicates the number of bytes to skip.
     */
    void SkipBytes(size_t bytes);

    /**
     * @brief Reads a C-style string from this parcel.
     *
     * @return Returns a pointer of the `char` type to the C-style string.
     */
    const char *ReadCString();

    /**
     * @brief Reads a C++ string (`std::string`) object from this parcel.
     *
     * @return Returns a pointer of the `std::string` type to the C-style
     * string.
     */
    const std::string ReadString();

    /**
     * @brief Reads a C++ string (`std::string`) object from this parcel to
     * an object.
     *
     * @param value Indicates the `std::string` object to hold the data read.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool ReadString(std::string &value);

    /**
     * @brief Reads a C++ UTF-16 encoded string (`std::u16string`) object
     * from this parcel.
     *
     * @return Returns a pointer of the `std::u16string` type to the C-style
     * string.
     */
    const std::u16string ReadString16();

    /**
     * @brief Reads a C++ UTF-16 string (`std::u16string`) object from this
     * parcel to an object.
     *
     * @param value Indicates the `std::u16string` object to hold the data read.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool ReadString16(std::u16string &value);

    /**
     * @brief Reads a C++ UTF-16 string (`std::u16string`) object and its length
     * from this parcel.
     *
     * @param len Indicates the reference to a variable of the `int32_t` type
     * to receive the length.
     * @return Returns an `std::u16string` object.
     */
    const std::u16string ReadString16WithLength(int32_t &len);

    /**
     * @brief Reads a C++ string (`std::string`) object and its length from
     * this parcel.
     *
     * @param len Indicates the reference to a variable of the `int32_t` type
     * to receive the length.
     * @return Returns an `std::string` object.
     */
    const std::string ReadString8WithLength(int32_t &len);

    /**
     * @brief Sets the read cursor to the specified position.
     *
     * @param newPosition Indicates the position, represented by the offset
     * (in bytes) from the beginning of the data region.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool RewindRead(size_t newPosition);

    /**
     * @brief Sets the write cursor to the specified position.
     *
     * @param offsets Indicates the position, represented by the offset
     * (in bytes) from the beginning of the data region.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool RewindWrite(size_t offsets);

    /**
     * @brief Obtains the current position of the read cursor.
     *
     * @return Returns the position, represented by the offset (in bytes)
     * from the beginning of the data region.
     */
    size_t GetReadPosition();

    /**
     * @brief Obtains the current position of the write cursor.
     *
     * @return Returns the position, represented by the offset (in bytes)
     * from the beginning of the data region.
     */
    size_t GetWritePosition();

    /**
     * @brief Reads a `Parcelable` object and its child class objects
     * from this parcel.
     *
     * @tparam T Indicates the class type of the output object.
     * @return Returns the object read.
     * @note A null pointer will be returned if '0' is read.
     */
    template <typename T>
    T *ReadParcelable();

    /**
     * @brief Reads a `Parcelable` object from this parcel and manages it by a
     * smart pointer.
     *
     * @tparam T Indicates the class type of the output object.
     * @return Returns the object managed by a smart pointer.
     * @note A null pointer will be returned if '0' is read.
     */
    template <typename T>
    sptr<T> ReadStrongParcelable();

    /**
     * @brief Checks whether it is valid to read an object from the current
     * cursor.
     *
     * @return Returns `true` if valid; returns `false` otherwise.
     */
    bool CheckOffsets();

    /**
     * @brief Reads an object from this parcel.
     *
     * Call `CheckOffsets()` first to check whether it is valid to read an
     * object.
     *
     * @tparam T Indicates the class type of the output object.
     * @return Returns the smart pointer to the object.
     * @note A null pointer will be returned if `CheckOffsets()` fails
     * to be called.
     */
    template<typename T>
    sptr<T> ReadObject();

    /**
     * @brief Sets a memory allocator for this parcel.
     *
     * The new allocator will reallocate the data region that has been written.
     *
     * @param allocator Indicates the pointer to an `Allocator` object.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool SetAllocator(Allocator *allocator);

    /**
     * @brief Records an array of positions of this parcel.
     *
     * @param offsets Indicates the pointer to the position array.
     * @param offsetSize Indicates the size of the position array.
     * @note The method returns directly if the call fail.
     */
    void InjectOffsets(binder_size_t offsets, size_t offsetSize);

    /**
     * @brief Deallocates the data region and resets this parcel.
     */
    void FlushBuffer();

    /**
     * @brief Writes an `std::vector` object to this parcel.
     *
     * @tparam T1 Indicates the class type for the vector.
     * @tparam T2 Indicates the class type for the write method of this parcel.
     * @param val Indicates the reference to the vector.
     * @param Write Indicates the `Parcel::Write(T2 value)` method.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    template <typename T1, typename T2>
    bool WriteVector(const std::vector<T1> &val, bool (Parcel::*Write)(T2));
    template <typename Type, typename T1, typename T2>
    bool WriteFixedAlignVector(const std::vector<T1> &originVal, bool (Parcel::*SpecialWrite)(T2));
    bool WriteBoolVector(const std::vector<bool> &val);
    bool WriteInt8Vector(const std::vector<int8_t> &val);
    bool WriteInt16Vector(const std::vector<int16_t> &val);
    bool WriteInt32Vector(const std::vector<int32_t> &val);
    bool WriteInt64Vector(const std::vector<int64_t> &val);
    bool WriteUInt8Vector(const std::vector<uint8_t> &val);
    bool WriteUInt16Vector(const std::vector<uint16_t> &val);
    bool WriteUInt32Vector(const std::vector<uint32_t> &val);
    bool WriteUInt64Vector(const std::vector<uint64_t> &val);
    bool WriteFloatVector(const std::vector<float> &val);
    bool WriteDoubleVector(const std::vector<double> &val);
    bool WriteStringVector(const std::vector<std::string> &val);
    bool WriteString16Vector(const std::vector<std::u16string> &val);

    /**
     * @brief Reads an `std::vector` object from this parcel.
     *
     * @tparam T1 Indicates the class type for the vector.
     * @tparam T2 Indicates the class type for the read method of this parcel.
     * @param val Indicates the pointer to the vector.
     * @param Write Indicates the `Parcel::Read(T2 value)` method.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    template <typename T>
    bool ReadVector(std::vector<T> *val, bool (Parcel::*Read)(T &));
    template <typename Type, typename T1, typename T2>
    bool ReadFixedAlignVector(std::vector<T1> *val, bool (Parcel::*SpecialRead)(T2 &));
    bool ReadBoolVector(std::vector<bool> *val);
    bool ReadInt8Vector(std::vector<int8_t> *val);
    bool ReadInt16Vector(std::vector<int16_t> *val);
    bool ReadInt32Vector(std::vector<int32_t> *val);
    bool ReadInt64Vector(std::vector<int64_t> *val);
    bool ReadUInt8Vector(std::vector<uint8_t> *val);
    bool ReadUInt16Vector(std::vector<uint16_t> *val);
    bool ReadUInt32Vector(std::vector<uint32_t> *val);
    bool ReadUInt64Vector(std::vector<uint64_t> *val);
    bool ReadFloatVector(std::vector<float> *val);
    bool ReadDoubleVector(std::vector<double> *val);
    bool ReadStringVector(std::vector<std::string> *val);
    bool ReadString16Vector(std::vector<std::u16string> *val);

    // write raw primitive type(i.e. 1byte for bool, 4byte for `int16_t`, etc.)
    bool WriteBoolUnaligned(bool value);
    bool WriteInt8Unaligned(int8_t value);
    bool WriteInt16Unaligned(int16_t value);
    bool WriteUint8Unaligned(uint8_t value);
    bool WriteUint16Unaligned(uint16_t value);
    // read raw primitive type(i.e. 1byte for bool, 4byte for `int16_t`, etc.)
    bool ReadBoolUnaligned();
    bool ReadInt8Unaligned(int8_t &value);
    bool ReadInt16Unaligned(int16_t &value);
    bool ReadUint8Unaligned(uint8_t &value);
    bool ReadUint16Unaligned(uint16_t &value);

protected:
    /**
     * @brief Records the position of the written object, which is represented
     * by the offset from the beginning of the data region.
     *
     * @param offset Indicates the position.
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool WriteObjectOffset(binder_size_t offset);

    /**
     * @brief Ensures that the number of written objects is less than
     * the capacity of objects.
     *
     * If the data region is full, the capacity will be expanded.
     *
     * @return Returns `true` if the operation is successful;
     * returns `false` otherwise.
     */
    bool EnsureObjectsCapacity();

private:
    DISALLOW_COPY_AND_MOVE(Parcel);
    template <typename T>
    bool Write(T value);

    template <typename T>
    bool Read(T &value);

    template <typename T>
    T Read();

    template <typename T>
    bool ReadPadded(T &value);

    inline size_t GetPadSize(size_t size)
    {
        const size_t SIZE_OFFSET = 3;
        return (((size + SIZE_OFFSET) & (~SIZE_OFFSET)) - size);
    }

    size_t CalcNewCapacity(size_t minCapacity);

    bool WriteDataBytes(const void *data, size_t size);

    void WritePadBytes(size_t padded);

    bool EnsureWritableCapacity(size_t desireCapacity);

    bool WriteParcelableOffset(size_t offset);

    const uint8_t *BasicReadBuffer(size_t length);
    
    bool IsReadObjectData(const size_t nextObj, const size_t upperBound);

    bool ValidateReadData(size_t upperBound);

    void ClearObjects();

private:
    uint8_t *data_;
    size_t readCursor_;
    size_t writeCursor_;
    size_t dataSize_;
    size_t dataCapacity_;
    size_t maxDataCapacity_;
    binder_size_t *objectOffsets_;
    size_t nextObjectIdx_;
    size_t objectCursor_;
    size_t objectsCapacity_;
    Allocator *allocator_;
    std::vector<sptr<Parcelable>> objectHolder_;
    bool writable_ = true;
};

template <typename T>
bool Parcel::WriteObject(const sptr<T> &object)
{
    if (object == nullptr) {
        return T::Marshalling(*this, object);
    }
    return WriteRemoteObject(object.GetRefPtr());
}

template <typename T>
sptr<T> Parcel::ReadObject()
{
    if (!this->CheckOffsets()) {
        return nullptr;
    }
    sptr<T> res(T::Unmarshalling(*this));
    return res;
}

// Read data from the given parcel into this parcelable object.
template <typename T>
T *Parcel::ReadParcelable()
{
    int32_t size = this->ReadInt32();
    if (size == 0) {
        return nullptr;
    }
    return T::Unmarshalling(*this);
}

// Read data from the given parcel into this parcelable object, and return sptr.
template <typename T>
sptr<T> Parcel::ReadStrongParcelable()
{
    int32_t size = this->ReadInt32();
    if (size == 0) {
        return nullptr;
    }
    sptr<T> res(T::Unmarshalling(*this));
    return res;
}

} // namespace OHOS
#endif
