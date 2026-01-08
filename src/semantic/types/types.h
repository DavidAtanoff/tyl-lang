// Tyl Compiler - Type System
#ifndef TYL_TYPES_H
#define TYL_TYPES_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace tyl {

struct Type;
struct TraitType;
using TypePtr = std::shared_ptr<Type>;
using TraitPtr = std::shared_ptr<TraitType>;

enum class TypeKind {
    VOID, BOOL, INT, INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64,
    FLOAT, FLOAT16, FLOAT32, FLOAT64, FLOAT128, STRING, CHAR, STR_VIEW, BYTE_ARRAY,
    LIST, MAP, RECORD, FUNCTION, PTR, REF,
    COMPLEX64, COMPLEX128,  // Complex number types
    BIGINT, BIGFLOAT, DECIMAL, RATIONAL,  // Arbitrary precision types
    FIXED,  // Fixed-point type
    VEC2, VEC3, VEC4, MAT2, MAT3, MAT4,  // SIMD vector/matrix types
    ANY, NEVER, UNKNOWN, ERROR,
    // New kinds for generics and traits
    TYPE_PARAM,     // Generic type parameter (e.g., T in fn swap[T])
    VALUE_PARAM,    // Value parameter for dependent types (e.g., N: int in Vector[T, N: int])
    GENERIC,        // Generic type instantiation (e.g., List[int])
    DEPENDENT,      // Dependent type (type that depends on values)
    REFINED,        // Refined type with constraint (e.g., NonEmpty[T] = [T] where len(_) > 0)
    TRAIT,          // Trait type
    TRAIT_OBJECT,   // Dynamic trait object (dyn Trait)
    FIXED_ARRAY,    // Fixed-size array (e.g., [int; 10])
    CHANNEL,        // Channel type for inter-thread communication (e.g., chan[int])
    MUTEX,          // Mutex type for mutual exclusion (e.g., Mutex[int])
    RWLOCK,         // Reader-writer lock type (e.g., RWLock[int])
    COND,           // Condition variable type
    SEMAPHORE,      // Counting semaphore type
    ATOMIC,         // Atomic type for lock-free operations (e.g., Atomic[int])
    FUTURE,         // Future type for async results (e.g., Future[int])
    THREAD_POOL,    // Thread pool type for worker threads
    CANCEL_TOKEN,   // Cancellation token type for task cancellation
    // Smart pointer types
    BOX,            // Box[T] - unique ownership heap allocation
    RC,             // Rc[T] - reference counted (single-threaded)
    ARC,            // Arc[T] - atomic reference counted (thread-safe)
    WEAK,           // Weak[T] - weak reference (non-owning)
    CELL,           // Cell[T] - interior mutability (single-threaded)
    REFCELL,        // RefCell[T] - runtime borrow checking
    // Algebraic effects
    EFFECT,         // Effect type (e.g., Error[str], State[int])
    EFFECTFUL,      // Function type with effects (e.g., fn() -> int with Error[str])
    // Higher-Kinded Types
    TYPE_CONSTRUCTOR,  // Type constructor (e.g., F[_] in trait Functor[F[_]])
    HKT_APPLICATION,   // Higher-kinded type application (e.g., F[A] where F is a type constructor)
    // Type Classes / Concepts
    CONCEPT            // Concept type (type class constraint)
};

struct Type {
    TypeKind kind;
    bool isMutable = true;
    bool isNullable = false;
    Type(TypeKind k) : kind(k) {}
    virtual ~Type() = default;
    virtual std::string toString() const;
    virtual bool equals(const Type* other) const;
    virtual TypePtr clone() const;
    bool isNumeric() const;
    bool isInteger() const;
    bool isFloat() const;
    bool isComplex() const;
    bool isPrimitive() const;
    bool isReference() const;
    bool isPointer() const;
    virtual size_t size() const;
    virtual size_t alignment() const;
};

struct PrimitiveType : Type {
    PrimitiveType(TypeKind k) : Type(k) {}
    std::string toString() const override;
    size_t size() const override;
};

struct PtrType : Type {
    TypePtr pointee;
    bool isRaw;
    PtrType(TypePtr p, bool raw = false) : Type(raw ? TypeKind::PTR : TypeKind::REF), pointee(std::move(p)), isRaw(raw) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

struct ListType : Type {
    TypePtr element;
    ListType(TypePtr elem) : Type(TypeKind::LIST), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

struct MapType : Type {
    TypePtr key;
    TypePtr value;
    MapType(TypePtr k, TypePtr v) : Type(TypeKind::MAP), key(std::move(k)), value(std::move(v)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

struct RecordField { std::string name; TypePtr type; bool hasDefault = false; };

struct RecordType : Type {
    std::string name;
    std::vector<RecordField> fields;
    RecordType(std::string n = "") : Type(TypeKind::RECORD), name(std::move(n)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
    TypePtr getField(const std::string& name) const;
};

struct FunctionType : Type {
    std::vector<std::pair<std::string, TypePtr>> params;
    TypePtr returnType;
    bool isVariadic = false;
    std::vector<std::string> typeParams;  // Generic type parameters
    FunctionType() : Type(TypeKind::FUNCTION) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Type parameter (e.g., T in fn swap[T])
struct TypeParamType : Type {
    std::string name;
    std::vector<std::string> bounds;  // Trait bounds (e.g., T: Printable + Comparable)
    TypePtr defaultType;              // Optional default type
    TypeParamType(std::string n) : Type(TypeKind::TYPE_PARAM), name(std::move(n)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
    bool satisfiesBound(const std::string& traitName) const;
};

// Value parameter for dependent types (e.g., N: int in Vector[T, N: int])
struct ValueParamType : Type {
    std::string name;                 // Parameter name (e.g., "N")
    TypePtr valueType;                // Type of the value (e.g., int)
    std::optional<int64_t> value;     // Concrete value if known at compile time
    ValueParamType(std::string n, TypePtr vt) : Type(TypeKind::VALUE_PARAM), name(std::move(n)), valueType(std::move(vt)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Dependent type definition (e.g., type Vector[T, N: int] = [T; N])
struct DependentType : Type {
    std::string name;                                    // Type name (e.g., "Vector")
    std::vector<std::pair<std::string, TypePtr>> params; // Type and value parameters
    TypePtr baseType;                                    // The underlying type (e.g., [T; N])
    DependentType(std::string n) : Type(TypeKind::DEPENDENT), name(std::move(n)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Refined type with constraint (e.g., type NonEmpty[T] = [T] where len(_) > 0)
struct RefinedType : Type {
    std::string name;                 // Type name (e.g., "NonEmpty")
    TypePtr baseType;                 // The underlying type (e.g., [T])
    std::string constraint;           // String representation of constraint for error messages
    // The actual constraint is evaluated at runtime or compile-time
    RefinedType(std::string n, TypePtr base, std::string constr = "") 
        : Type(TypeKind::REFINED), name(std::move(n)), baseType(std::move(base)), constraint(std::move(constr)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Trait method signature
struct TraitMethod {
    std::string name;
    std::shared_ptr<FunctionType> signature;
    bool hasDefaultImpl = false;
};

// Trait type definition
struct TraitType : Type {
    std::string name;
    std::vector<std::string> typeParams;           // Generic params for the trait itself
    std::vector<TraitMethod> methods;              // Required methods
    std::vector<std::string> superTraits;          // Inherited traits
    TraitType(std::string n) : Type(TypeKind::TRAIT), name(std::move(n)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
    const TraitMethod* getMethod(const std::string& methodName) const;
};

// Trait object type (dyn Trait)
struct TraitObjectType : Type {
    std::string traitName;
    TraitPtr trait;
    TraitObjectType(std::string n, TraitPtr t = nullptr) 
        : Type(TypeKind::TRAIT_OBJECT), traitName(std::move(n)), trait(std::move(t)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Concept requirement (function signature in a concept)
struct ConceptRequirementType {
    std::string name;
    std::shared_ptr<FunctionType> signature;
    bool isStatic = false;  // Static function (no self parameter)
};

// Concept type (type class / constrained generic)
struct ConceptType : Type {
    std::string name;
    std::vector<std::string> typeParams;           // Type parameters [T]
    std::vector<ConceptRequirementType> requirements;  // Required functions
    std::vector<std::string> superConcepts;        // Inherited concepts
    ConceptType(std::string n) : Type(TypeKind::CONCEPT), name(std::move(n)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
    const ConceptRequirementType* getRequirement(const std::string& reqName) const;
};
using ConceptPtr = std::shared_ptr<ConceptType>;

// Generic type instantiation (e.g., Pair[int, str])
struct GenericType : Type {
    std::string baseName;                          // Base type name (e.g., "Pair")
    std::vector<TypePtr> typeArgs;                 // Type arguments
    TypePtr resolvedType;                          // Resolved concrete type after substitution
    GenericType(std::string base) : Type(TypeKind::GENERIC), baseName(std::move(base)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Fixed-size array type (e.g., [int; 10], [[int; 3]; 4])
struct FixedArrayType : Type {
    TypePtr element;                               // Element type (can be another FixedArrayType for multi-dim)
    size_t size;                                   // Number of elements
    FixedArrayType(TypePtr elem, size_t sz) : Type(TypeKind::FIXED_ARRAY), element(std::move(elem)), size(sz) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
    size_t totalSize() const;                      // Total size in bytes (element size * count)
    size_t elementSize() const;                    // Size of one element in bytes
    size_t dimensions() const;                     // Number of dimensions (1 for [T;N], 2 for [[T;N];M], etc.)
    std::vector<size_t> shape() const;             // Shape of the array (e.g., {4, 3} for [[int;3];4])
};

// Channel type for inter-thread communication (e.g., chan[int], chan[int, 10])
struct ChannelType : Type {
    TypePtr element;                               // Element type being sent/received
    size_t bufferSize;                             // Buffer capacity (0 = unbuffered/synchronous)
    ChannelType(TypePtr elem, size_t buf = 0) : Type(TypeKind::CHANNEL), element(std::move(elem)), bufferSize(buf) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Mutex type for mutual exclusion (e.g., Mutex[int])
struct MutexType : Type {
    TypePtr element;                               // Protected data type
    MutexType(TypePtr elem) : Type(TypeKind::MUTEX), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Reader-writer lock type (e.g., RWLock[int])
struct RWLockType : Type {
    TypePtr element;                               // Protected data type
    RWLockType(TypePtr elem) : Type(TypeKind::RWLOCK), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Condition variable type
struct CondType : Type {
    CondType() : Type(TypeKind::COND) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Counting semaphore type
struct SemaphoreType : Type {
    SemaphoreType() : Type(TypeKind::SEMAPHORE) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Atomic type for lock-free operations (e.g., Atomic[int])
struct AtomicType : Type {
    TypePtr element;                               // Element type (must be integer type)
    AtomicType(TypePtr elem) : Type(TypeKind::ATOMIC), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Future type for async results (e.g., Future[int])
struct FutureType : Type {
    TypePtr element;                               // Result type
    FutureType(TypePtr elem) : Type(TypeKind::FUTURE), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Thread pool type for worker threads
struct ThreadPoolType : Type {
    ThreadPoolType() : Type(TypeKind::THREAD_POOL) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Cancellation token type for task cancellation
struct CancelTokenType : Type {
    CancelTokenType() : Type(TypeKind::CANCEL_TOKEN) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Box type for unique ownership heap allocation (e.g., Box[int])
struct BoxType : Type {
    TypePtr element;                               // Contained type
    BoxType(TypePtr elem) : Type(TypeKind::BOX), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Rc type for reference counting (single-threaded) (e.g., Rc[int])
struct RcType : Type {
    TypePtr element;                               // Contained type
    RcType(TypePtr elem) : Type(TypeKind::RC), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Arc type for atomic reference counting (thread-safe) (e.g., Arc[int])
struct ArcType : Type {
    TypePtr element;                               // Contained type
    ArcType(TypePtr elem) : Type(TypeKind::ARC), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Weak type for non-owning references (e.g., Weak[int])
struct WeakType : Type {
    TypePtr element;                               // Referenced type
    bool isAtomic;                                 // true for Weak from Arc, false for Weak from Rc
    WeakType(TypePtr elem, bool atomic = false) : Type(TypeKind::WEAK), element(std::move(elem)), isAtomic(atomic) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Cell type for interior mutability (single-threaded) (e.g., Cell[int])
struct CellType : Type {
    TypePtr element;                               // Contained type (must be Copy)
    CellType(TypePtr elem) : Type(TypeKind::CELL), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// RefCell type for runtime borrow checking (e.g., RefCell[int])
struct RefCellType : Type {
    TypePtr element;                               // Contained type
    RefCellType(TypePtr elem) : Type(TypeKind::REFCELL), element(std::move(elem)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// BigInt type for arbitrary precision integers
struct BigIntType : Type {
    BigIntType() : Type(TypeKind::BIGINT) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// BigFloat type for arbitrary precision floats
struct BigFloatType : Type {
    BigFloatType() : Type(TypeKind::BIGFLOAT) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Decimal type for exact decimal arithmetic (financial)
struct DecimalType : Type {
    DecimalType() : Type(TypeKind::DECIMAL) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Rational type for exact fractions (num/denom)
struct RationalType : Type {
    RationalType() : Type(TypeKind::RATIONAL) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Fixed-point type (e.g., Fixed[32, 16] = 32 total bits, 16 fractional)
struct FixedPointType : Type {
    size_t totalBits;      // Total number of bits
    size_t fracBits;       // Number of fractional bits
    FixedPointType(size_t total, size_t frac) : Type(TypeKind::FIXED), totalBits(total), fracBits(frac) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// SIMD Vector types (Vec2, Vec3, Vec4)
struct VecType : Type {
    TypePtr element;       // Element type (typically f32 or f64)
    size_t size;           // 2, 3, or 4
    VecType(TypeKind k, TypePtr elem, size_t sz) : Type(k), element(std::move(elem)), size(sz) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// SIMD Matrix types (Mat2, Mat3, Mat4)
struct MatType : Type {
    TypePtr element;       // Element type (typically f32 or f64)
    size_t size;           // 2, 3, or 4 (square matrices)
    MatType(TypeKind k, TypePtr elem, size_t sz) : Type(k), element(std::move(elem)), size(sz) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Effect operation signature
struct EffectOperation {
    std::string name;                              // Operation name (e.g., "raise", "get", "put")
    std::vector<std::pair<std::string, TypePtr>> params;  // Parameters
    TypePtr returnType;                            // Return type
};

// Effect type (e.g., Error[str], State[int])
struct EffectType : Type {
    std::string name;                              // Effect name (e.g., "Error", "State")
    std::vector<TypePtr> typeArgs;                 // Type arguments (e.g., [str] for Error[str])
    std::vector<EffectOperation> operations;       // Effect operations
    EffectType(std::string n) : Type(TypeKind::EFFECT), name(std::move(n)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
    const EffectOperation* getOperation(const std::string& opName) const;
};

// Effectful function type (function with effects)
struct EffectfulType : Type {
    TypePtr baseType;                              // The underlying function type
    std::vector<std::shared_ptr<EffectType>> effects;  // Effects this function may perform
    EffectfulType(TypePtr base) : Type(TypeKind::EFFECTFUL), baseType(std::move(base)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Higher-Kinded Type: Type constructor parameter (e.g., F[_] in trait Functor[F[_]])
// Represents a type that takes type parameters (like List, Option, Result)
struct TypeConstructorType : Type {
    std::string name;                              // Constructor name (e.g., "F")
    size_t arity;                                  // Number of type parameters it expects (e.g., 1 for F[_], 2 for F[_, _])
    std::vector<std::string> bounds;               // Trait bounds (e.g., F[_]: Functor)
    TypeConstructorType(std::string n, size_t ar = 1) 
        : Type(TypeKind::TYPE_CONSTRUCTOR), name(std::move(n)), arity(ar) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Higher-Kinded Type Application (e.g., F[A] where F is a type constructor)
// Represents applying a type constructor to concrete type arguments
struct HKTApplicationType : Type {
    std::string constructorName;                   // Name of the type constructor (e.g., "F")
    TypePtr constructor;                           // The type constructor being applied (TypeConstructorType)
    std::vector<TypePtr> typeArgs;                 // Type arguments being applied
    HKTApplicationType(std::string name) 
        : Type(TypeKind::HKT_APPLICATION), constructorName(std::move(name)) {}
    std::string toString() const override;
    bool equals(const Type* other) const override;
    TypePtr clone() const override;
};

// Trait implementation record
struct TraitImpl {
    std::string traitName;
    std::string typeName;
    std::vector<TypePtr> typeArgs;                 // Type arguments for generic impls
    std::unordered_map<std::string, std::shared_ptr<FunctionType>> methods;
};

class TypeRegistry {
public:
    static TypeRegistry& instance();
    TypePtr voidType(); TypePtr boolType(); TypePtr intType();
    TypePtr int8Type(); TypePtr int16Type(); TypePtr int32Type(); TypePtr int64Type();
    TypePtr uint8Type(); TypePtr uint16Type(); TypePtr uint32Type(); TypePtr uint64Type();
    TypePtr floatType(); TypePtr float16Type(); TypePtr float32Type(); TypePtr float64Type(); TypePtr float128Type();
    TypePtr complex64Type(); TypePtr complex128Type();
    TypePtr stringType(); TypePtr charType(); TypePtr strViewType(); TypePtr byteArrayType();
    TypePtr anyType(); TypePtr neverType(); TypePtr unknownType(); TypePtr errorType();
    TypePtr ptrType(TypePtr pointee, bool raw = false);
    TypePtr refType(TypePtr pointee);
    TypePtr listType(TypePtr element);
    TypePtr mapType(TypePtr key, TypePtr value);
    TypePtr recordType(const std::string& name);
    TypePtr functionType();
    TypePtr fromString(const std::string& str);
    void registerType(const std::string& name, TypePtr type);
    TypePtr lookupType(const std::string& name);
    
    // Generic and trait support
    TypePtr typeParamType(const std::string& name);
    TypePtr genericType(const std::string& baseName, const std::vector<TypePtr>& typeArgs);
    TraitPtr traitType(const std::string& name);
    TypePtr traitObjectType(const std::string& traitName);
    TypePtr fixedArrayType(TypePtr element, size_t size);  // Fixed-size array type
    TypePtr channelType(TypePtr element, size_t bufferSize = 0);  // Channel type for inter-thread communication
    TypePtr mutexType(TypePtr element);  // Mutex type for mutual exclusion
    TypePtr rwlockType(TypePtr element);  // Reader-writer lock type
    TypePtr condType();  // Condition variable type
    TypePtr semaphoreType();  // Counting semaphore type
    TypePtr atomicType(TypePtr element);  // Atomic type for lock-free operations
    TypePtr futureType(TypePtr element);  // Future type for async results
    TypePtr threadPoolType();  // Thread pool type for worker threads
    TypePtr cancelTokenType();  // Cancellation token type for task cancellation
    
    // Smart pointer types
    TypePtr boxType(TypePtr element);      // Box[T] - unique ownership heap allocation
    TypePtr rcType(TypePtr element);       // Rc[T] - reference counted (single-threaded)
    TypePtr arcType(TypePtr element);      // Arc[T] - atomic reference counted (thread-safe)
    TypePtr weakType(TypePtr element, bool isAtomic = false);  // Weak[T] - weak reference
    TypePtr cellType(TypePtr element);     // Cell[T] - interior mutability
    TypePtr refCellType(TypePtr element);  // RefCell[T] - runtime borrow checking
    
    // Arbitrary precision and fixed-point types
    TypePtr bigIntType();      // Arbitrary precision integer
    TypePtr bigFloatType();    // Arbitrary precision float
    TypePtr decimalType();     // Exact decimal arithmetic
    TypePtr rationalType();    // Exact fractions
    TypePtr fixedPointType(size_t totalBits, size_t fracBits);  // Fixed-point type
    
    // SIMD vector and matrix types
    TypePtr vec2Type(TypePtr element);   // 2D vector
    TypePtr vec3Type(TypePtr element);   // 3D vector
    TypePtr vec4Type(TypePtr element);   // 4D vector
    TypePtr mat2Type(TypePtr element);   // 2x2 matrix
    TypePtr mat3Type(TypePtr element);   // 3x3 matrix
    TypePtr mat4Type(TypePtr element);   // 4x4 matrix
    
    // Trait registration and lookup
    void registerTrait(const std::string& name, TraitPtr trait);
    TraitPtr lookupTrait(const std::string& name);
    
    // Trait implementation management
    void registerTraitImpl(const TraitImpl& impl);
    const TraitImpl* lookupTraitImpl(const std::string& traitName, const std::string& typeName);
    bool typeImplementsTrait(TypePtr type, const std::string& traitName);
    std::vector<const TraitImpl*> getTraitImpls(const std::string& typeName);
    
    // Generic type instantiation
    TypePtr instantiateGeneric(TypePtr genericType, const std::vector<TypePtr>& typeArgs);
    TypePtr substituteTypeParams(TypePtr type, const std::unordered_map<std::string, TypePtr>& substitutions);
    
    // Type constraint checking
    bool checkTraitBounds(TypePtr type, const std::vector<std::string>& bounds);
    
    // Dependent type support
    TypePtr valueParamType(const std::string& name, TypePtr valueType);  // Value parameter (N: int)
    TypePtr dependentType(const std::string& name);                       // Dependent type definition
    TypePtr refinedType(const std::string& name, TypePtr baseType, const std::string& constraint = "");  // Refined type with constraint
    void registerDependentType(const std::string& name, TypePtr type);    // Register a dependent type
    TypePtr lookupDependentType(const std::string& name);                 // Lookup a dependent type
    TypePtr instantiateDependentType(const std::string& name, const std::vector<std::pair<std::string, int64_t>>& valueArgs, const std::vector<TypePtr>& typeArgs);  // Instantiate with concrete values
    bool checkRefinementConstraint(TypePtr type, const std::string& constraint);  // Check if value satisfies constraint
    
    // Algebraic effects support
    std::shared_ptr<EffectType> effectType(const std::string& name);      // Create/lookup effect type
    TypePtr effectfulType(TypePtr baseType, const std::vector<std::shared_ptr<EffectType>>& effects);  // Function with effects
    void registerEffect(const std::string& name, std::shared_ptr<EffectType> effect);  // Register an effect
    std::shared_ptr<EffectType> lookupEffect(const std::string& name);    // Lookup an effect by name
    
    // Higher-Kinded Types support
    TypePtr typeConstructorType(const std::string& name, size_t arity = 1);  // Create type constructor (F[_])
    TypePtr hktApplicationType(const std::string& constructorName, const std::vector<TypePtr>& typeArgs);  // Apply type constructor
    void registerTypeConstructor(const std::string& name, TypePtr constructor);  // Register a type constructor
    TypePtr lookupTypeConstructor(const std::string& name);  // Lookup a type constructor
    bool isTypeConstructor(const std::string& name);  // Check if name is a registered type constructor
    TypePtr applyTypeConstructor(TypePtr constructor, const std::vector<TypePtr>& args);  // Apply constructor to args
    
    // Type Classes / Concepts support
    ConceptPtr conceptType(const std::string& name);  // Create/lookup concept type
    void registerConcept(const std::string& name, ConceptPtr concept);  // Register a concept
    ConceptPtr lookupConcept(const std::string& name);  // Lookup a concept by name
    bool typeImplementsConcept(TypePtr type, const std::string& conceptName);  // Check if type satisfies concept
    bool checkConceptConstraints(TypePtr type, const std::vector<std::string>& conceptNames);  // Check multiple concept constraints
    
private:
    TypeRegistry();
    std::unordered_map<std::string, TypePtr> namedTypes_;
    std::unordered_map<std::string, TraitPtr> traits_;
    std::unordered_map<std::string, ConceptPtr> concepts_;  // Registered concepts
    std::unordered_map<std::string, TypePtr> dependentTypes_;  // Dependent type definitions
    std::unordered_map<std::string, std::shared_ptr<EffectType>> effects_;  // Registered effects
    std::unordered_map<std::string, TypePtr> typeConstructors_;  // Higher-kinded type constructors
    std::vector<TraitImpl> traitImpls_;
    TypePtr void_, bool_, int_, int8_, int16_, int32_, int64_;
    TypePtr uint8_, uint16_, uint32_, uint64_;
    TypePtr float_, float16_, float32_, float64_, float128_, string_, char_, str_view_, byte_array_;
    TypePtr complex64_, complex128_;
    TypePtr bigint_, bigfloat_, decimal_, rational_;
    TypePtr any_, never_, unknown_, error_;
};

} // namespace tyl

#endif // TYL_TYPES_H
