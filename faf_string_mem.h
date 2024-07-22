#ifndef FAF_STRING_MEM_H

// TODO: so I think the general idea is to have a fix sized memory pool heap.
// TODO: lets think; we'll have str_split return an array, how will the memory
//       on it be managed? Would need to either have manual memory management on
//       the pointer, or have a new type and manually manage it somehow.
//       Does it ever make sense to have a struct that is essentially an array type,
//       that behaves like a C++ collection where the stack allocated object cleans
//       up the heap memory of the object when it falls out of scope? That's essentially
//       what a destructor is, which we don't have. I guess this is good practice because
//       it's making me think abou this in ways I haven't before.

#endif // FAF_STRING_MEM_H
