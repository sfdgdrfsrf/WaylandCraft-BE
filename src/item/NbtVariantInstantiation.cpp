// ============================================================================
//  WaylandCraft-BE — src/item/NbtVariantInstantiation.cpp
//
//  Explicit instantiation of the Bedrock NBT tag variant that backs
//  CompoundTagVariant (std::variant over the 12 Tag types).
//
//  Why this file exists: writing NBT (CompoundTagVariant::operator=) makes
//  the compiler emit references to the recursive std::_Variant_storage_
//  destructor chain. Those symbols live inside the BDS binary, whose
//  toolset's STL generates a different (older, non-recursive) shape than the
//  toolset LeviLamina mods are built with today, so the prelink-generated
//  import library does not carry them. Explicitly instantiating the variant
//  here defines every member locally and resolves the references.
//
//  [namespace.std] sanctions exactly this: an explicit instantiation of a
//  standard library template is well-formed when it depends on a
//  user-defined type with external linkage — Bedrock's Tag types qualify.
// ============================================================================

#include "mc/deps/nbt/ByteArrayTag.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/DoubleTag.h"
#include "mc/deps/nbt/EndTag.h"
#include "mc/deps/nbt/FloatTag.h"
#include "mc/deps/nbt/Int64Tag.h"
#include "mc/deps/nbt/IntArrayTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/deps/nbt/StringTag.h"

template class ::std::variant<
    ::EndTag,
    ::ByteTag,
    ::ShortTag,
    ::IntTag,
    ::Int64Tag,
    ::FloatTag,
    ::DoubleTag,
    ::ByteArrayTag,
    ::StringTag,
    ::ListTag,
    ::CompoundTag,
    ::IntArrayTag>;
