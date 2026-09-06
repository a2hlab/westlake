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
 * @file refbase.h
 *
 * @brief Provide smart pointer implemented in c_utils.
 */

 /**
  * @defgroup SmartPointer
  * @{
  * @brief SmartPointer is a pointer-like class.
  *
  * This class simulates a pointer while providing
  * enhanced features, such as automatic memory management.\n
  * Automatic memory management helps to deallocate
  * memory resources correctly when an object's life time expires.
  */

#ifndef UTILS_BASE_REFBASE_H
#define UTILS_BASE_REFBASE_H

#include <atomic>
#include <functional>
#ifdef DEBUG_REFBASE
#include <mutex>
#endif

namespace OHOS {
/**
 * @ingroup SmartPointer
 * @brief A value indicates no strong references ever.
 */
#define INITIAL_PRIMARY_VALUE (1 << 28)

class RefBase;

#if ((defined DEBUG_REFBASE) && (!defined PRINT_TRACK_AT_ONCE))
class RefTracker;
#endif

/**
 * @ingroup SmartPointer
 * @brief Reference counter class. Such a class records two kinds
 * of references to the corresponding RefBase object and one kind
 * of references to the RefCounter object itself.
 *
 * There are two different references for a single object.\n
 * A strong reference holds the reference directly pointing to the object,
 * and the object should be alive as long as the strong reference exists.\n
 * A weak reference holds a reference indirectly pointing to the object,
 * and the object is not always alive/existent
 * even if the weak reference exists.
 * @note The preceding descriptions are valid only when smart pointers
 * are properly used.
 */
class RefCounter {
public:
    /**
     * @brief Callback function used to destroy the corresponding
     * RefBase object.
     */
    using RefPtrCallback = std::function<void()>;
    friend class RefBase;

    RefCounter();

    explicit RefCounter(RefCounter *counter);

    RefCounter &operator=(const RefCounter &counter);

    virtual ~RefCounter();

    /**
     * @brief Sets the callback function.
     *
     * @param callback Callback function used to delete
     * the corresponding RefBase object.
     */
    void SetCallback(const RefPtrCallback& callback);

    /**
     * @brief Removes the current callback function by setting it to a `nullptr`.
     */
    void RemoveCallback();

    /**
     * @brief Gets the count of references to the RefCounter object.
     *
     * @return Count of references to the RefCounter object.
     */
    int GetRefCount();

    /**
     * @brief Increments the count of references to the RefCounter object by 1.
     */
    void IncRefCount();

    /**
     * @brief Decrements the count of references to the RefCounter object by 1.
     *
     * Once the count reaches 0 after being decremented,
     * `delete this` will be called to deallocate this RefCounter object.
     */
    void DecRefCount();

    /**
     * @brief Checks if the pointer to the callback function is a `nullptr`.
     *
     * @return `true` if the callback function is a `nullptr`;
     * `false` otherwise.
     */
    bool IsRefPtrValid();

    /**
     * @brief Increments the count of strong references to the
     * corresponding RefBase object by 1.
     *
     * @return Original count before increment.
     */
    int IncStrongRefCount(const void *objectId);

    /**
     * @brief Decrements the count of strong references to the
     * corresponding RefBase object by 1.
     *
     * @return Original count before decrement.
     * @note If the strong reference never exists,
     * decrement will be ineffective.
     */
    int DecStrongRefCount(const void *objectId);

    /**
     * @brief Gets the count of strong references to the
     * corresponding RefBase object.
     *
     * @return Count of strong references.
     */
    int GetStrongRefCount();

    /**
     * @brief Increments the count weak references to the
     * corresponding RefBase object by 1.
     *
     * @return Original count before increment.
     */
    int IncWeakRefCount(const void *objectId);

    /**
     * @brief Decrements the count of weak references to the
     * corresponding RefBase object by 1.
     *
     * @return Original (before decrement) value of atomicWeak_.
     * @note If the count reaches 0 after being decremented, the
     * corresponding RefBase object with no strong reference ever,
     * or the RefBase object with 0 strong reference but an extended
     * 'life time', will be deallocated.
     */
    int DecWeakRefCount(const void *objectId);

    /**
     * @brief Gets the count of weak references to the
     * corresponding RefBase object.
     *
     * @return Count of weak references.
     */
    int GetWeakRefCount();

    /**
     * @brief Sets the number of attempts to increment.
     */
    void SetAttemptAcquire();

    /**
     * @brief Get the current times of attempts,this interface is only for IPC use.
     */
    int GetAttemptAcquire();

    /**
     * @brief Check if the number of attempts is greater than 0.
     *
     * @return `true` if the number of attempts is greater than 0;
     * `false` otherwise.
     */
    bool IsAttemptAcquireSet();

    /**
     * @brief Clears number of attempts to increment.
     */
    void ClearAttemptAcquire();

    /**
     * @brief Attempts to increment the count of strong references to the
     * corresponding RefBase object by 1.
     *
     * @param outCount If the attempt success, the original value
     * (before increment) of the count will be stored here.
     * @return `true` if the attempt is successful; `false` otherwise.
     */
    bool AttemptIncStrongRef(const void *objectId, int &outCount);

    // Only for IPC use.
    /**
     * @brief Attempts to increment the count of strong references to the
     * the corresponding RefBase object by 1 (simplified).
     *
     * @return `true` if the attempt is successful; `false` otherwise.
     * @note Only for IPC use.
     */
    bool AttemptIncStrong(const void *objectId);

    /**
     * @brief Checks if the corresponding RefBase object
     * has an extended life-time.
     *
     * @return `true` if success; `false` otherwise.
     */
    bool IsLifeTimeExtended();

    /**
     * @brief Extends the life-time of corresponding RefBase object.
     *
     * This allows the corresponding object keep alive
     * even if there is no strong reference.
     * @note The RefBase object will be deallocated
     * if the count of weak references also reaches 0.
     */
    void ExtendObjectLifetime();

#ifdef OHOS_PLATFORM
    using CanPromote = std::function<bool()>;

    void SetCanPromote(const CanPromote& canPromote);

    void RemoveCanPromote();

    bool IsCanPromoteValid();
#endif

#if ((defined DEBUG_REFBASE) && (!defined TRACK_ALL))
    /**
     * @brief Enables tracking. It is applicable to debugging only.
     */
    void EnableTracker();
#endif

#if ((defined DEBUG_REFBASE) && (defined PRINT_TRACK_AT_ONCE))
    /**
     * @brief Enables tracking with domainId. It is applicable to debugging only.
     */
    void EnableTrackerWithDomainId(unsigned int domainId);
#endif

private:
    void DebugRefBase(const void *objectId);

    void RefBaseDebugPrint(int curCount, const void* caller, const void* objectId,
        const char* operation, const char* countType);
        
    std::atomic<int> atomicStrong_; // = (num of sptr) or Initial-value
    std::atomic<int> atomicWeak_; // = (num of sptr)+(num of WeakRefCounter)
    std::atomic<int> atomicRefCount_; // = (num of WeakRefCounter) + 1
    std::atomic<unsigned int> atomicFlags_; // Life-time extended flag
    std::atomic<int> atomicAttempt_; // Number of attempts
    RefPtrCallback callback_ = nullptr; // Callback function to deallocate the corresponding RefBase object
    static constexpr unsigned int FLAG_EXTEND_LIFE_TIME = 0x00000002; // Extended life-time bit to be set via logic-OR
#ifdef OHOS_PLATFORM
    CanPromote canPromote_ = nullptr;
#endif
#ifdef DEBUG_REFBASE
#ifdef TRACK_ALL
    bool enableTrack = true;
#else
    bool enableTrack = false;
#endif
    std::mutex trackerMutex;  // To ensure refTracker be thread-safe
#ifdef PRINT_TRACK_AT_ONCE
    unsigned int domainId_ = 0xD003D00;
#else
    RefTracker* refTracker = nullptr;
    void GetNewTrace(const void* objectId);
    void PrintTracker();
#endif
#endif
};

/**
 * @ingroup SmartPointer
 * @brief An intermediate class to represent the weak reference
 * to the correspond RefBase object.
 *
 * A WeakRefCounter object can be shared by multiple wptr objects.\n
 * It holds the references to the corresponding RefBase and RefCounter object.
 * Those two references will be set as `nullptr` when the weak referenced
 * target and its RefCounter object are deallocated. The WeakRefCounter
 * object can remain alive even if the referenced target is deallocated.
 */
class WeakRefCounter {
public:
    /**
     * @brief Constructs a WeakRefCounter object.
     *
     * @param counter Pointer to corresponding RefCounter object.
     * @param cookie Pointer to corresponding RefBase object.
     */
    WeakRefCounter(RefCounter *counter, void *cookie);

    virtual ~WeakRefCounter();

    /**
     * @brief Gets the current pointer to the corresponding RefBase object.
     *
     * @return A void pointer to the RefBase object.
     * If the corresponding object is not alive, a `nullptr` will be returned.
     * @note A void pointer means that you should cast it to the real type,
     * since it can be any subclass of RefBase.
     */
    void *GetRefPtr();

    /**
     * @brief Increments the count of references to this WeakRefCounter object.
     *
     * @note This count is different from the count of weak references
     * in RefCounter. It is equal to the count of wptrs directly
     * referenced to this WeakRefCount object.
     * @see RefCounter
     */
    void IncWeakRefCount(const void *objectId);

    /**
     * @brief Decrements the count of references to this WeakRefCounter object.
     *
     * @note This WeakRefCounter object will be deallocated when this count
     * reaches 0.
     */
    void DecWeakRefCount(const void *objectId);

    /**
     * @brief Gets the count recorded by this WeakRefCounter object.
     *
     * @return Count recorded by this WeakRefCounter object.
     * @note The count is different from that in RefCounter.
     * @see RefCounter::GetWeakRefCount()
     */
    int GetWeakRefCount() const;

    /**
     * @brief Attempts to increment the count of strong references to
     * the corresponding RefBase object (to promote a wptr to a sptr).
     *
     * @return `true` after a success increment.
     */
    bool AttemptIncStrongRef(const void *objectId);

private:
    std::atomic<int> atomicWeak_; // Count of references to this
                                  // WeakRefCounter object
                                  // The value is equal to the count of wptrs
                                  // that references this WeakRefCounter object.
    RefCounter *refCounter_ = nullptr; // Reference to the RefCounter object of
                                       // the corresponding RefBase object
    void *cookie_ = nullptr; // Pointer to the corresponding RefBase object
};

/**
 * @ingroup SmartPointer
 * @brief A base class of subclasses that can be managed by SmartPointer.
 *
 * @note All classes which intend to be managed by SmartPointer should be
 * derived from RefBase.
 */
class RefBase {
public:
    RefBase();

    /**
     * @brief Copy constructor of RefBase.
     *
     * @note This function constructs a new RefCounter object
     * and binds it to the RefBase object.
     */
    RefBase(const RefBase &);

    /**
     * @brief Copy assignment operator of RefBase.
     *
     * @note This function unbinds this RefBase object from the
     * original RefCounter object, and then binds it to the
     * newly constructed RefCounter object.
     */
    RefBase &operator=(const RefBase &);

    /**
     * @brief Move constructor of RefBase.
     */
    RefBase(RefBase &&other) noexcept;

    /**
     * @brief Move assignment operator of RefBase.
     *
     * @note This function binds this RefBase object with the RefCounter
     * object of the argument `other`, which will unbind the
     * RefCounter object. No counts operation will be processed.
     */
    RefBase &operator=(RefBase &&other) noexcept;

    virtual ~RefBase();

    /**
     * @brief Callback function to deallocate this object.
     *
     * This function provides the default implementation to deallocate
     * this RefBase object by simply calling `delete(this)`.
     */
    virtual void RefPtrCallback();

    /**
     * @brief Extends the life time of the RefBase object.
     *
     * @note The object whose life time has been extended will not be
     * deallocated if the count of weak references, instead of strong
     * references, reaches 0.
     */
    void ExtendObjectLifetime();

    /**
     * @brief Increments the count of strong references.
     *
     * `OnFirstStrongRef()`, which is an empty function by default,
     * will be called when the first strong reference is established.
     *
     * @note This function automatically increments the count of weak
     * references meanwhile.
     */
    void IncStrongRef(const void *objectId);

    /**
     * @brief Decrements the count of strong references.
     *
     * If the life time is not extended, this object will be deallocated
     * when the count of strong references reaches 0.\n
     * `OnLastStrongRef()`, which is an empty function by default,
     * will be called when the last strong reference is deleted.
     */
    void DecStrongRef(const void *objectId);

    /**
     * @brief Gets the count of strong references.
     *
     * @return Count of strong references. The value is 0 if the
     * corresponding RefCounter object does not exist.
     * @note This function is valid only when corresponding RefCounter
     * object exists.
     */
    int GetSptrRefCount();

   /**
    * @brief Creates a weak reference to this RefBase object.
    *
    * @param cookie Void pointer to this RefBase object.
    * @return Pointer to the newly created WeakRefCounter object.
    * @note Use this function with related functions of wptr.
    * Do not use it independently.
    */
    WeakRefCounter *CreateWeakRef(void *cookie);

    /**
     * @brief Gets the pointer to corresponding counter object.
     *
     * @return Pointer to the counter object.
     */
    RefCounter *GetRefCounter() const;

    /**
     * @brief Increments the count of weak references.
     *
     * @note This function is valid only when corresponding RefCounter
     * object exists.
     */
    void IncWeakRef(const void *objectId);

    /**
     * @brief Decrements the count of weak references.
     *
     * @note This function is valid only when corresponding RefCounter
     * object exists.
     */
    void DecWeakRef(const void *objectId);

    /**
     * @brief Gets the count of weak references.
     *
     * @return Count of weak references. The value is 0 if the corresponding
     * RefCounter object does not exist.
     */
    int GetWptrRefCount();

    /**
     * @brief Attempts to increment the count of strong references.
     *
     * `OnFirstStrongRef()`, which is an empty function by default, will be
     * called when the first strong reference is established.
     *
     * @return `true` if the increment is successful; `false` otherwise.
     * @note The count of attempts will increment by 1
     * after a successful increment.
     */
    bool AttemptAcquire(const void *objectId);

    /**
     * @brief Check attempt acquire is setted,this interface is only for IPC use.
     * This interface must come after the RefBase::AttemptIncStrong or RefBase::AttemptAcquire function,
     * And ensuring thread safety in multi-threaded scenarios.
     *
     * @note This fuction is extracted from `IncStrongRef()`
     * It is only for IPC use.
     */
    void CheckIsAttemptAcquireSet(const void *objectId);

    /**
     * @brief Attempts to increment the count of strong references.
     *
     * `OnFirstStrongRef()`, which is an empty function by default, will be
     * called when the first strong reference is established.
     * @return `true` if the increment is successful; `false` otherwise.
     * @note Use this function in the copy constructor of sptr in scenario of
     * interaction between sptr and wptr. Avoid using it independently.
     */
    bool AttemptIncStrongRef(const void *objectId);

    // Only for IPC use.
    /**
     * @brief Attempts to increment the count of strong references.
     *
     * @return `true` if the increment is successful; `false` otherwise.
     * @note If the operation is successful, the count of successful attempts
     * will increment by 1.
     * @note This function is a simplified version of `AttemptAcquire`.
     * It is only for IPC use.
     */
    bool AttemptIncStrong(const void *objectId);

    /**
     * @brief Checks if the count of successful attempts is greater than 0.
     *
     * @return `true` if the count of successful attempts is greater than 0;
     * `false` if the count of successful attempts is not greater than 0
     * or the corresponding RefCounter object does not exist.
     */
    bool IsAttemptAcquireSet();

    /**
     * @brief Checks if the life time of this RefBase object has been extended.
     *
     * @return `true` if the life time of this RefBase object has been extended;
     * `false` if the RefBase object has a normal life time or the corresponding
     * RefCounter object does not exist.
     */
    bool IsExtendLifeTimeSet();

    /**
     * @brief Called when the first strong reference is established.
     *
     * @note It is an empty function by default.
     */
    virtual void OnFirstStrongRef(const void *);

    /**
     * @brief Called when the last strong reference is deleted.
     *
     * @note It is an empty function by default.
     */
    virtual void OnLastStrongRef(const void *);

    /**
     * @brief Called when the last weak reference is deleted.
     *
     * @note It is an empty function by default.
     */
    virtual void OnLastWeakRef(const void *);

    /**
     * @brief Called when `wptr::Promote()` is invoked.
     *
     * @return `true` if success; `false` otherwise.
     */
    virtual bool OnAttemptPromoted(const void *);

    /**
     * @brief Enables tracking of the RefBase object. This function will
     * be implemented only if DEBUG_REFBASE, but not TRACK_ALL, is defined.
     */
    void EnableTracker();

    /**
     * @brief Enables tracking of the RefBase object with domainId. This function will
     * be implemented only if DEBUG_REFBASE, but not TRACK_ALL, is defined.
     */
    void EnableTrackerWithDomainId(unsigned int domainId);

#ifdef OHOS_PLATFORM
    virtual bool CanPromote();
#endif

private:
    RefCounter *refs_ = nullptr; // Pointer to the corresponding reference
                                 // counter of this RefBase object
};

template <typename T>
class wptr;

/**
 * @ingroup SmartPointer
 * @brief Strong reference smart pointer to a RefBase object
 * (or an object of its subclass).
 *
 * It directly reference the RefBase object.
 *
 * @tparam T Specific class type managed by sptr.
 * This class must inherit from RefBase.
 */
template <typename T>
class sptr {
    friend class wptr<T>;

public:
    sptr();

    ~sptr();

    /**
     * @brief Create a new object with class type (T) and provide a new sptr to manage it.
     *
     * @note We strongly recommend using `sptr::MakeSptr` to create a object and manage it.
     * This approach avoids object pointer leaks, which can avoid many potential memory problems.
     *
     * @return A sptr which manage a new object with class type (T).
     * @param args Constructor parameters of the new object to be managed by sptr.
     */
    template <typename... Args>
    static inline sptr<T> MakeSptr(Args&&... args);

    /**
     * @brief Constructor with the specified object to be managed.
     * And We do not recommend using this interface to create an sptr object.
     * Using the interface `sptr::MakeSptr` is better
     *
     * @note A null sptr will be created if `other` is `nullptr`.
     * @param other Object to be managed by sptr.
     */
    sptr(T *other);

    /**
     * @brief Copy constructor for sptr with the managed class type (T).
     *
     * @param other Input sptr object.
     */
    sptr(const sptr<T> &other);

    /**
     * @brief Move constructor.
     *
     * @note `other` will be set to a null sptr.
     * @param other Input sptr object.
     */
    sptr(sptr<T> &&other);

    /**
     * @brief Move assignment operator.
     *
     * @param other Input sptr object.
     * @note The original strong reference in target sptr object will
     * be removed.
     */
    sptr<T> &operator=(sptr<T> &&other);

    /**
     * @brief Copy Constructor for sptr with the managed class type (O).
     *
     * @tparam O Another specific class type managed by `other`.
     * @param other Input sptr object.
     */
    template <typename O>
    sptr(const sptr<O> &other);

    /**
     * @brief Constructor used to promote the process of wptr.
     *
     * @param p WeakRefCounter object which hold the reference to the
     * managed object.
     * @param force Used to distinguish from other constructors.
     */
    inline sptr(WeakRefCounter *p, bool force);

    /**
     * @brief Gets the pointer to the managed object.
     *
     * @return Pointer of the specific managed class type.
     */
    inline T *GetRefPtr() const
    {
        return refs_;
    }

    /**
     * @brief Sets the pointer to the managed object.
     *
     * @param other Another pointer object to be managed by sptr.
     * @note Avoid using this function independently. Otherwise,
     * a mismatch of the reference count will arise, leading to memory problems.
     */
    inline void ForceSetRefPtr(T *other);

    /**
     * @brief Removes the reference to the managed object held by current sptr.
     *
     * @note This function will make this sptr a "null sptr".
     */
    void clear();

    /**
     * @brief Type conversion operator.
     *
     * @return Raw pointer to the managed object.
     * @note The sptr object will not be converted. Only the member raw
     * pointer will be returned.
     */
    inline operator T *() const
    {
        return refs_;
    }

    /**
     * @brief Explicit boolean conversion operator.
     *
     * @return `true` if refbase object is not a "null ptr"; `false` otherwise.
     */
    inline explicit operator bool() const
    {
        return refs_ != nullptr;
    }

    /**
     * @brief Dereference operator.
     *
     * This function will return the object managed by this sptr.
     *
     * @return Reference to the specific object managed by sptr.
     */
    inline T &operator*() const
    {
        return *refs_;
    }

    /**
     * @brief Member selection operator.
     *
     * This function will return the specified member of the object
     * managed by this sptr.
     */
    inline T *operator->() const
    {
        return refs_;
    }

    /**
     * @brief Copy assignment operator with the specified object to be managed.
     * And We do not recommend using this interface to create an sptr object.
     * Using the interface `sptr::MakeSptr` is better.
     *
     * @note The original reference will be removed, and a new reference to the
     * input object will be established.
     * @param other Another object to be managed by this sptr.
     */
    sptr<T> &operator=(T *other);

    /**
     * @brief Copy assignment operator for sptr with
     * the same managed class type (T).
     *
     * @note The original reference will be removed, and the same object
     * with the input sptr object will be managed by this sptr.
     * @param other Another sptr object with the same managed class type (T).
     */
    sptr<T> &operator=(const sptr<T> &other);

    /**
     * @brief Copy assignment operator for wptr with
     * the same managed class type (T).
     *
     * @note The original reference will be removed, and the same object
     * with the input wptr object will be managed by this sptr.
     * @note If the operation fails, this sptr will turn to be a "null sptr".
     * @param other Another wptr object with the same managed class type (T).
     */
    sptr<T> &operator=(const wptr<T> &other);

    /**
     * @brief Copy assignment operator for sptr with
     * a different managed class type (O).
     *
     * @note The original reference will be removed, and the same object
     * with the input sptr object will be managed by this sptr.
     * @note This sptr will interpret the managed object as the type (T).
     * @param other Another sptr object with a different managed class type (O).
     */
    template <typename O>
    sptr<T> &operator=(const sptr<O> &other);

    /**
     * @brief Equal-to operator between the sptr and raw pointer.
     *
     * @param other Input raw pointer.
     * @return `true` if the sptr points to the same object with input
     * raw pointer; `false` otherwise.
     */
    bool operator==(const T *other) const;

    /**
     * @brief Not-equal-to operator between the sptr and raw pointer.
     *
     * @param other Input raw pointer.
     * @return `true` if the sptr does not point to the same object
     * with input raw pointer; `false` otherwise.
     */
    inline bool operator!=(const T *other) const
    {
        return !operator==(other);
    }

    /**
     * @brief Equal-to operator between the sptr and wptr.
     *
     * @param other Input wptr.
     * @return `true` if the same object is managed by the sptr and wptr;
     * `false` otherwise.
     */
    bool operator==(const wptr<T> &other) const;

    /**
     * @brief Not-equal-to operator between the sptr and wptr.
     *
     * @param other Input wptr.
     * @return `true` if different objects are managed by the sptr and wptr;
     * `false` otherwise.
     */
    inline bool operator!=(const wptr<T> &other) const
    {
        return !operator==(other);
    }

    /**
     * @brief Equal-to operator between two sptrs.
     *
     * @param other Input sptr.
     * @return `true` if the same object is managed by two sptrs;
     * `false` otherwise.
     */
    bool operator==(const sptr<T> &other) const;

    /**
     * @brief Not-equal-to operator between sptrs.
     *
     * @param other Input sptr.
     * @return `true` if different objects are managed by two sptrs;
     * `false` otherwise.
     */
    inline bool operator!=(const sptr<T> &other) const
    {
        return !operator==(other);
    }

private:
    T *refs_ = nullptr; // Raw pointer to the specific managed object
};

template <typename T>
template <typename... Args>
sptr<T> sptr<T>::MakeSptr(Args&&... args)
{
    T *ptr = new T(std::forward<Args>(args)...);
    sptr<T> res;
    res.ForceSetRefPtr(ptr);
    ptr->IncStrongRef(ptr);
    return res;
}

template <typename T>
inline void sptr<T>::ForceSetRefPtr(T *other)
{
    refs_ = other;
}

template <typename T>
inline sptr<T>::sptr()
{
    refs_ = nullptr;
}

template <typename T>
inline sptr<T>::sptr(T *other)
{
    refs_ = other;
    if (refs_ != nullptr) {
        refs_->IncStrongRef(this);
    }
}

template <typename T>
inline sptr<T>::sptr(const sptr<T> &other)
{
    refs_ = other.GetRefPtr();
    if (refs_ != nullptr) {
        refs_->IncStrongRef(this);
    }
}

template <typename T>
sptr<T>::sptr(sptr<T> &&other)
{
    refs_ = other.GetRefPtr();
    other.ForceSetRefPtr(nullptr);
}

template <typename T>
sptr<T> &sptr<T>::operator=(sptr<T> &&other)
{
    if (refs_ != nullptr) {
        refs_->DecStrongRef(this);
    }
    refs_ = other.GetRefPtr();
    other.ForceSetRefPtr(nullptr);
    return *this;
}

template <typename T>
template <typename O>
sptr<T>::sptr(const sptr<O> &other) : refs_(other.GetRefPtr())
{
    if (refs_ != nullptr) {
        refs_->IncStrongRef(this);
    }
}

template <typename T>
inline sptr<T> &sptr<T>::operator=(T *other)
{
    if (other != nullptr) {
        other->IncStrongRef(this);
    }

    if (refs_ != nullptr) {
        refs_->DecStrongRef(this);
    }

    refs_ = other;
    return *this;
}

template <typename T>
inline sptr<T> &sptr<T>::operator=(const sptr<T> &other)
{
    T *otherRef(other.GetRefPtr());
    if (otherRef != nullptr) {
        otherRef->IncStrongRef(this);
    }

    if (refs_ != nullptr) {
        refs_->DecStrongRef(this);
    }

    refs_ = otherRef;
    return *this;
}

template <typename T>
inline sptr<T> &sptr<T>::operator=(const wptr<T> &other)
{
    if (refs_ != nullptr) {
        refs_->DecStrongRef(this);
    }
    if ((other != nullptr) && other.AttemptIncStrongRef(this)) {
        refs_ = other.GetRefPtr();
    } else {
        refs_ = nullptr;
    }

    return *this;
}

template <typename T>
template <typename O>
sptr<T> &sptr<T>::operator=(const sptr<O> &other)
{
    T *otherRef(other.GetRefPtr());
    if (otherRef != nullptr) {
        otherRef->IncStrongRef(this);
    }

    if (refs_ != nullptr) {
        refs_->DecStrongRef(this);
    }

    refs_ = otherRef;
    return *this;
}

template <typename T>
inline bool sptr<T>::operator==(const T *other) const
{
    return other == refs_;
}

template <typename T>
inline bool sptr<T>::operator==(const wptr<T> &other) const
{
    return refs_ == other.GetRefPtr();
}

template <typename T>
inline bool sptr<T>::operator==(const sptr<T> &other) const
{
    return refs_ == other.GetRefPtr();
}

template<typename T>
void sptr<T>::clear()
{
    if (refs_) {
        refs_->DecStrongRef(this);
        refs_ = 0;
    }
}

template <typename T>
inline sptr<T>::~sptr()
{
    if (refs_ != nullptr) {
        refs_->DecStrongRef(this);
    }
}

template <typename T>
inline sptr<T>::sptr(WeakRefCounter *p, bool /* force */)
{
    if ((p != nullptr) && p->AttemptIncStrongRef(this)) {
        refs_ = reinterpret_cast<T *>(p->GetRefPtr());
    } else {
        refs_ = nullptr;
    }
}

/**
 * @ingroup SmartPointer
 * @brief Weak reference smart pointer to a RefBase object
 * (or an object of its subclass).
 *
 * A weak reference indirectly references the RefBase object
 * and directly references the WeakRefCounter object.
 *
 * @tparam T Specific class type managed by wptr.
 * This class must inherit from RefBase.
 */
template <typename T>
class wptr {
    template <typename O>
    friend class wptr;

public:
    wptr();

    /**
     * @brief Constructor with the specified object to be managed.
     *
     * This function will create WeakRefCounter object for `other`
     * and set the count of weak references to 1.
     *
     * @note A WeakRefCounter object will not be created if `other`
     * is a `nullptr`.
     *
     * @param other Object to be managed by wptr.
     */
    wptr(T *other);

    /**
     * @brief Copy constructor for wptr with the same managed class type (T).
     *
     * This function will share the WeakRefCounter object of `other` with this
     * wptr and set the count of weak references properly.
     *
     * @param other Another wptr with the same managed class type (T).
     */
    wptr(const wptr<T> &other);

    /**
     * @brief Copy constructor for sptr with the same managed class type (T).
     *
     * This function will create a WeakRefCounter object for the managed
     * object of `other`, and set the count of weak references properly.
     *
     * @param other Another sptr with the same managed class type (T).
     * @tparam T Specific class type managed by `other`.
     */
    wptr(const sptr<T> &other);

    /**
     * @brief Copy constructor for wptr with a different managed class type (O).
     *
     * This function is the same as wptr<T>::wptr(const wptr<T> &other).
     *
     * @tparam O Class type managed by `other`.
     * @param other Another wptr with a different managed class type (O).
     * @tparam T Specific class type managed by `other`.
     */
    template <typename O>
    wptr(const wptr<O> &other);

    /**
     * @brief Copy constructor for sptr with a different managed class type (O).
     *
     * This function is the same as wptr<T>::wptr(const sptr<T> &other).
     *
     * @param other Another sptr with the same managed class type (O).
     * @tparam T Specific class type managed by `other`.
     */
    template <typename O>
    wptr(const sptr<O> &other);

    /**
     * @brief Copy assignment operator with the specified object to be managed.
     *
     * @note The current wptr will unbind the original WeakRefCounter object,
     * create a new WeakRefCounter object, and then set the count of weak
     * references properly.
     * @param other Another object to be managed by this wptr.
     */
    wptr<T> &operator=(T *other);

    /**
     * @brief Copy assignment operator with the specified object to be managed.
     *
     * @note Same with wptr<T> &operator=(T *other), but a pointer type casting
     * which will not affect the type of `*other` is processed.
     * @tparam O Specific class type managed by `other`.
     * @param other Another object to be managed by this wptr.
     *
     */
    template <typename O>
    wptr<T> &operator=(O *other);

    /**
     * @brief Copy assignment operator for wptr with the same managed class
     * type (T).
     *
     * @note The current wptr will unbind the original WeakRefCounter object,
     * share the WeakRefCounter object with `other`, and then set the count of
     * weak references properly.
     * @param other Another wptr object. Objects managed by it will also be
     * managed by this wptr.
     */
    wptr<T> &operator=(const wptr<T> &other);

    /**
     * @brief Copy assignment operator for sptr with the same managed class
     * type (T).
     *
     * @note The current wptr will unbind the original WeakRefCounter object,
     * create a new WeakRefCounter object, and then set the count of weak
     * references properly.
     * @param other A sptr object. Objects managed by it will also be
     * managed by this wptr.
     */
    wptr<T> &operator=(const sptr<T> &other);

    /**
     * @brief Copy assignment operator for wptr with a different managed class
     * type (O).
     *
     * @note This function is the same as wptr<T> &operator=(const wptr<T> &).
     * Note that no cast here is processed.
     * @param other An wptr object. Objects managed by it will also be
     * managed by this wptr.
     * @tparam O Specific class type managed by `other`.
     */
    template <typename O>
    wptr<T> &operator=(const wptr<O> &other);

    /**
     * @brief Copy assignment operator for sptr with a different managed class
     * type (O).
     *
     * @note This function is the same as
     * wptr<T> &wptr<T>::operator=(const sptr<T> &).
     * Note that no cast here is processed.
     * @param other An sptr object. Objects managed by it will also be
     * managed by this wptr.
     * @tparam O Specific class type managed by `other`.
     */
    template <typename O>
    wptr<T> &operator=(const sptr<O> &other);

    /**
     * @brief Dereference operator.
     *
     * This function will return the object managed by this wptr.
     *
     * @return Specific object managed by wptr.
     */
    inline T &operator*() const
    {
        return *GetRefPtr();
    }

    /**
     * @brief Member selection operator.
     *
     * This function will return the specified object member managed
     * by this wptr.
     */
    inline T *operator->() const
    {
        return reinterpret_cast<T *>(refs_->GetRefPtr());
    }

    /**
     * @brief Equal-to operator between the wptr and raw pointer.
     *
     * @param other Input raw pointer.
     * @return `true` if two pointers have the same value; `false` otherwise.
     */
    bool operator==(const T *other) const;

    /**
     * @brief Not-equal-to operator between the wptr and raw pointer.
     *
     * @param other Input raw pointer.
     * @return `true` if two pointers have different values; `false` otherwise.
     */
    inline bool operator!=(const T *other) const
    {
        return !operator==(other);
    };

    /**
     * @brief Equal-to operator between two wptrs.
     *
     * @param other Input reference to a wptr object.
     * @return `true` if two pointers have the same value; `false` otherwise.
     */
    bool operator==(const wptr<T> &other) const;

    /**
     * @brief Not-equal-to operator between two wptrs.
     *
     * @param other Input reference to a wptr object.
     * @return `true` if two pointers have different values; `false` otherwise.
     */
    inline bool operator!=(const wptr<T> &other) const
    {
        return !operator==(other);
    }

    /**
     * @brief Equal-to operator between the wptr and input sptr object.
     *
     * @param other Input reference to an sptr object.
     * @return true` if two pointers have the same value; `false` otherwise.
     */
    bool operator==(const sptr<T> &other) const;

    /**
     * @brief Not-equal-to operator between the wptr and input sptr object.
     *
     * @param other Input reference to an sptr object.
     * @return `true` if two pointers have different values; `false` otherwise.
     */
    inline bool operator!=(const sptr<T> &other) const
    {
        return !operator==(other);
    }

    /**
     * @brief Gets the pointer to the RefBase object.
     *
     * @return Raw pointer to the RefBase object.
     * @note `nullptr` will be returned if the managed object has been
     * deallocated.
     */
    T *GetRefPtr() const;

    /**
     * @brief Gets the count of weak references in a WeakRefCounter object.
     *
     * The value indicates how many wptrs share the same WeakRefCounter object.
     *
     * @return Count of weak references.
     * @note Only for test.
     */
    inline int GetWeakRefCount() const
    {
        return refs_->GetWeakRefCount();
    }

    /**
     * @brief Attempts to increment the count of strong references in
     * the managed object.
     *
     * @return `true` if the increment is successful; `false` otherwise.
     * @note Avoid using this function independently. Use `promote()` instead.
     */
    inline bool AttemptIncStrongRef(const void *objectId) const
    {
        return refs_->AttemptIncStrongRef(objectId);
    }

    /**
     * @brief Promotes a wptr to an sptr.
     *
     * This function will create an sptr object based on the object
     * managed by this wptr.
     *
     * @note The original weak reference will be retained.
     * If the promotion fails, a "null sptr" will be returned.
     */
    const sptr<T> promote() const;

    ~wptr();

private:
    WeakRefCounter *refs_ = nullptr; // Pointer to the corresponding WeakRefCounter object
};

template <typename T>
inline T *wptr<T>::GetRefPtr() const
{
    return (refs_ != nullptr) ? reinterpret_cast<T *>(refs_->GetRefPtr()) : nullptr;
}

template <typename T>
wptr<T>::wptr()
{
    refs_ = nullptr;
}

template <typename T>
wptr<T>::wptr(T *other)
{
    if (other != nullptr) {
        refs_ = other->CreateWeakRef(other);
        if (refs_ != nullptr) {
            refs_->IncWeakRefCount(this);
        }
    } else {
        refs_ = nullptr;
    }
}

template <typename T>
wptr<T>::wptr(const wptr<T> &other)
{
    refs_ = other.refs_;
    if (refs_ != nullptr) {
        refs_->IncWeakRefCount(this);
    }
}

template <typename T>
wptr<T>::wptr(const sptr<T> &other)
{
    if (other.GetRefPtr() != nullptr) {
        refs_ = other->CreateWeakRef(other.GetRefPtr());
        if (refs_ != nullptr) {
            refs_->IncWeakRefCount(this);
        }
    }
}

template <typename T>
template <typename O>
wptr<T>::wptr(const wptr<O> &other)
{
    refs_ = other.refs_;
    if (refs_ != nullptr) {
        refs_->IncWeakRefCount(this);
    }
}

template <typename T>
template <typename O>
wptr<T>::wptr(const sptr<O> &other)
{
    if (other.GetRefPtr() != nullptr) {
        refs_ = other->CreateWeakRef(other.GetRefPtr());
        if (refs_ != nullptr) {
            refs_->IncWeakRefCount(this);
        }
    }
}

template <typename T>
wptr<T> &wptr<T>::operator=(T *other)
{
    WeakRefCounter *newWeakRef = nullptr;
    if (other != nullptr) {
        newWeakRef = other->CreateWeakRef(other);
        if (newWeakRef != nullptr) {
            newWeakRef->IncWeakRefCount(this);
        }
    }

    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }

    refs_ = newWeakRef;
    return *this;
}

template <typename T>
template <typename O>
wptr<T> &wptr<T>::operator=(O *other)
{
    T *object = reinterpret_cast<T *>(other);
    WeakRefCounter *newWeakRef = nullptr;
    if (object != nullptr) {
        newWeakRef = object->CreateWeakRef(object);
        if (newWeakRef != nullptr) {
            newWeakRef->IncWeakRefCount(this);
        }
    }

    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }

    refs_ = newWeakRef;
    return *this;
}

template <typename T>
inline wptr<T> &wptr<T>::operator=(const wptr<T> &other)
{
    if (other.refs_ != nullptr) {
        other.refs_->IncWeakRefCount(this);
    }

    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }

    refs_ = other.refs_;
    return *this;
}

template <typename T>
inline wptr<T> &wptr<T>::operator=(const sptr<T> &other)
{
    WeakRefCounter *newWeakRef = nullptr;
    if (other.GetRefPtr() != nullptr) {
        newWeakRef = other->CreateWeakRef(other.GetRefPtr());
        if (newWeakRef != nullptr) {
            newWeakRef->IncWeakRefCount(this);
        }
    }

    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }

    refs_ = newWeakRef;
    return *this;
}

template <typename T>
template <typename O>
wptr<T> &wptr<T>::operator=(const wptr<O> &other)
{
    if (other.refs_ != nullptr) {
        other.refs_->IncWeakRefCount(this);
    }

    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }

    refs_ = other.refs_;
    return *this;
}

template <typename T>
template <typename O>
wptr<T> &wptr<T>::operator=(const sptr<O> &other)
{
    WeakRefCounter *newWeakRef = nullptr;
    if (other.GetRefPtr() != nullptr) {
        newWeakRef = other->CreateWeakRef(other.GetRefPtr());
        if (newWeakRef != nullptr) {
            newWeakRef->IncWeakRefCount(this);
        }
    }

    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }

    refs_ = newWeakRef;
    return *this;
}

template <typename T>
inline bool wptr<T>::operator==(const T *other) const
{
    return GetRefPtr() == other;
}

template <typename T>
inline bool wptr<T>::operator==(const wptr<T> &other) const
{
    return GetRefPtr() == other.GetRefPtr();
}

template <typename T>
inline bool wptr<T>::operator==(const sptr<T> &other) const
{
    return GetRefPtr() == other.GetRefPtr();
}

template <typename T>
inline const sptr<T> wptr<T>::promote() const
{
    return sptr<T>(refs_, true);
}

template <typename T>
inline wptr<T>::~wptr()
{
    if (refs_ != nullptr) {
        refs_->DecWeakRefCount(this);
    }
}

} // namespace OHOS

#endif

/**@}*/
