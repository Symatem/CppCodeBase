#include <Storage/Blob.hpp>

bool unlink(Symbol symbol);

template<bool guarded, typename ElementType>
struct BlobVector {
    Symbol symbol;

    BlobVector() :symbol(0) {}

    ~BlobVector() {
        if(guarded && symbol)
            releaseSymbol(symbol);
    }

    bool empty() const {
        return (!symbol || Blob(symbol).getSize() == 0);
    }

    NativeNaturalType size() const {
        return (symbol) ? Blob(symbol).getSize()/sizeOfInBits<ElementType>::value : 0;
    }

    ElementType readElementAt(NativeNaturalType offset) const {
        assert(symbol && offset < size());
        return Blob(symbol).readAt<ElementType>(offset);
    }

    void writeElementAt(NativeNaturalType offset, ElementType element) const {
        assert(symbol && offset < size());
        Blob(symbol).writeAt<ElementType>(offset, element);
    }

    void swapElementsAt(NativeNaturalType a, NativeNaturalType b) const {
        assert(a < size() && b < size());
        ElementType A = readElementAt(a),
                    B = readElementAt(b);
        writeElementAt(a, B);
        writeElementAt(b, A);
    }

    ElementType front() const {
        return readElementAt(0);
    }

    ElementType back() const {
        return readElementAt(size()-1);
    }

    void iterate(Closure<void(ElementType)> callback) const {
        for(NativeNaturalType at = 0; at < size(); ++at)
            callback(readElementAt(at));
    }

    void activate() {
        if(!symbol) {
            symbol = createSymbol();
            assert(guarded && symbol);
        }
    }

    void reserve(NativeNaturalType size) {
        activate();
        Blob(symbol).setSize(size*sizeOfInBits<ElementType>::value);
    }

    void clear() {
        reserve(0);
    }

    void insert(NativeNaturalType offset, ElementType element) {
        activate();
        Blob dstBlob(symbol);
        assert(dstBlob.increaseSize(offset*sizeOfInBits<ElementType>::value, sizeOfInBits<ElementType>::value));
        dstBlob.writeAt<ElementType>(offset, element);
    }

    void erase(NativeNaturalType offset, NativeNaturalType length) {
        assert(symbol);
        assert(Blob(symbol).decreaseSize(offset*sizeOfInBits<ElementType>::value, sizeOfInBits<ElementType>::value));
    }

    void erase(NativeNaturalType at) {
        erase(at, 1);
    }

    void push_back(ElementType element) {
        insert(size(), element);
    }

    ElementType pop_back() {
        assert(!empty());
        ElementType element = back();
        erase(size()-1);
        return element;
    }
};

template<typename KeyType, typename ValueType = VoidType[0]>
struct Pair {
    KeyType key;
    ValueType value;
    Pair() {}
    Pair(KeyType _key) :key(_key) {}
    Pair(KeyType _key, ValueType _value) :key(_key), value(_value) {}
    operator KeyType() {
        return key;
    }
};

template<bool guarded, typename KeyType, typename ValueType>
struct BlobMap : public BlobVector<guarded, Pair<KeyType, ValueType>> {
    typedef Pair<KeyType, ValueType> ElementType;
    typedef BlobVector<guarded, ElementType> Super;

    BlobMap() :Super() {}

    void iterateKeys(Closure<void(KeyType)> callback) const {
        for(NativeNaturalType at = 0; at < Super::size(); ++at)
            callback(Super::readElementAt(at).key);
    }

    KeyType&& key(NativeNaturalType at) const {
        assert(Super::symbol && at < Super::size());
        KeyType key;
        Blob(Super::symbol).externalOperate<false>(&key, at*sizeOfInBits<ElementType>::value, sizeOfInBits<KeyType>::value);
        return reinterpret_cast<ValueType&&>(key);
    }

    ValueType&& value(NativeNaturalType at) const {
        assert(Super::symbol && at < Super::size());
        ValueType value;
        Blob(Super::symbol).externalOperate<false>(&value, at*sizeOfInBits<ElementType>::value+sizeOfInBits<KeyType>::value, sizeOfInBits<ValueType>::value);
        return reinterpret_cast<ValueType&&>(value);
    }

    bool writeKeyAt(NativeNaturalType at, KeyType key) const {
        assert(Super::symbol && at < Super::size());
        Blob(Super::symbol).externalOperate<true>(&key, at*sizeOfInBits<ElementType>::value, sizeOfInBits<KeyType>::value);
        return true;
    }

    bool writeValueAt(NativeNaturalType at, ValueType value) const {
        assert(Super::symbol && at < Super::size());
        Blob(Super::symbol).externalOperate<true>(&value, at*sizeOfInBits<ElementType>::value+sizeOfInBits<KeyType>::value, sizeOfInBits<ValueType>::value);
        return true;
    }
};

template<bool guarded, typename KeyType, typename ValueType = VoidType[0]>
struct BlobHeap : public BlobMap<guarded, KeyType, ValueType> {
    typedef BlobMap<guarded, KeyType, ValueType> Super;
    typedef typename Super::ElementType ElementType;

    BlobHeap() :Super() {}

    void siftToLeaves(NativeNaturalType at, NativeNaturalType size) {
        while(true) {
            NativeNaturalType left = 2*at+1,
                              right = 2*at+2,
                              min = at;
            if(left < size && Super::key(left) < Super::key(min))
                min = left;
            if(right < size && Super::key(right) < Super::key(min))
                min = right;
            if(min == at)
                break;
            Super::swapElementsAt(at, min);
            at = min;
        }
    }

    void siftToLeaves(NativeNaturalType at) {
        siftToLeaves(at, Super::size());
    }

    void build() {
        for(NativeIntegerType i = Super::size()/2-1; i >= 0; --i)
            siftToLeaves(i);
    }

    void sort() {
        build();
        NativeNaturalType size = Super::size();
        while(size > 1) {
            --size;
            Super::swapElementsAt(0, size);
            siftToLeaves(0, size);
        }
    }

    void siftToRoot(NativeNaturalType at) {
        while(at > 0) {
            NativeNaturalType parent = (at-1)/2;
            if(Super::key(parent) <= Super::key(at))
                break;
            Super::swapElementsAt(at, parent);
            at = parent;
        }
    }

    void insertElement(ElementType element) {
        Super::push_back(element);
        siftToRoot(Super::size()-1);
    }

    void erase(NativeNaturalType at) {
        NativeNaturalType last = Super::size()-1;
        if(at != last)
            Super::writeElementAt(at, Super::readElementAt(last));
        Super::pop_back();
        if(at != last) {
            if(at == 0 || Super::key((at-1)/2) < Super::key(at))
                siftToLeaves(at);
            else
                siftToRoot(at);
        }
    }

    bool writeKeyAt(NativeNaturalType at, KeyType key) const {
        Super::writeKeyAt(at, key);
        siftToRoot(at);
        return true;
    }
};

template<bool guarded, typename KeyType, typename ValueType = VoidType[0]>
struct BlobSet : public BlobMap<guarded, KeyType, ValueType> {
    typedef BlobMap<guarded, KeyType, ValueType> Super;
    typedef typename Super::ElementType ElementType;

    BlobSet() :Super() {}

    NativeNaturalType find(KeyType key) const {
        return binarySearch<NativeNaturalType>(Super::size(), [&](NativeNaturalType at) {
            return key > Super::readElementAt(at).key;
        });
    }

    bool find(KeyType key, NativeNaturalType& at) const {
        at = find(key);
        return (at < Super::size() && Super::readElementAt(at).key == key);
    }

    bool insertElement(ElementType element) {
        NativeNaturalType at;
        if(find(element.key, at))
            return false;
        Super::insert(at, element);
        return true;
    }

    bool eraseElement(ElementType element) {
        NativeNaturalType at;
        if(!find(element.key, at))
            return false;
        Super::erase(at);
        return true;
    }

    bool writeKeyAt(NativeNaturalType at, KeyType key) const {
        assert(Super::symbol && at < Super::size());
        NativeNaturalType newAt;
        ValueType value = Super::value(at);
        if(find(key, newAt))
            return false;
        Super::erase(at);
        Super::insert(newAt, {key, value});
        return true;
    }
};

template<bool guarded>
struct BlobIndex : public BlobSet<guarded, Symbol> {
    typedef BlobSet<guarded, Symbol> Super;

    BlobIndex() :Super() {}

    NativeNaturalType find(Symbol key) const {
        return binarySearch<NativeNaturalType>(Super::size(), [&](NativeNaturalType at) {
            return Blob(key).compare(Blob(Super::readElementAt(at))) < 0;
        });
    }

    bool find(Symbol element, NativeNaturalType& at) const {
        at = find(element);
        if(at == Super::size())
            return false;
        return (Blob(element).compare(Blob(Super::readElementAt(at))) == 0);
    }

    void insertElement(Symbol& element) {
        NativeNaturalType at;
        if(find(element, at)) {
            unlink(element);
            element = Super::readElementAt(at);
        } else
            Super::insert(at, element);
    }

    bool eraseElement(Symbol element) {
        NativeNaturalType at;
        if(!find(element, at))
            return false;
        Super::erase(at);
        return true;
    }
};
