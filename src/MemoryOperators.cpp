// LeviLamina memory operator override — standard practice from the official
// plugin template (keeps the mod's allocations inside LeviLamina's shared
// allocator domain on Windows).
#define LL_MEMORY_OPERATORS
