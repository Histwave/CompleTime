#include <stdint.h>
#include <stdbool.h>

/* Minimal Test Examples for Showing how CT-LLVM Works. */


// Example where Flowtraceker cannot detect the leakage
int example1(int secret)
{
  volatile uint64_t array[5] = {1, 2, 3, 4, 5};
  uint64_t address = (uint64_t)&array[0] + secret * sizeof(uint64_t);
  return *((uint64_t *) address); // Leak via Cache
}

// Example where ctchecker cannot detect the leakage
int example2(int secret)
{
  uint64_t array[5] = {1, 2, 3, 4, 5};
  volatile uint64_t tmp = array[secret]; // Leak via Cache
  return 1;
} 

//Example of Flow sensitive analysis
int example3(int secret)
{
  int array[5] = {1, 2, 3, 4, 5};
  if (array[0]) {;} // False Positive
  array[0] = secret;
  if (array[0]) {;} // True Positive
  return 1;
}

//Example of Interprocedural analysis
int callee(uint64_t sec_addres) 
{
  return *((uint64_t *) sec_addres); // Leak via Cache
}

int caller(int secret)
{
  uint64_t array[5] = {1, 2, 3, 4, 5};
  uint64_t sec_addres = (uint64_t)&array[0] + secret * sizeof(uint64_t);
  return callee(sec_addres);
}


// Example of Leak via Branch
int example4(int secret)
{
  if (secret) {;} // Leak via Branch
  return 1;
}

// No False positive on comparing address 
// But do report loaded value
int example5(int secret, int *sec_ptr)
{
  uint64_t sec_addres = (uint64_t)&secret;
  if (sec_addres > 0) {;} // False Positive
  
  if (sec_ptr == ((void*)0)) {;} // False Positive

  if (secret > 0) {;} // True Positive
  if (sec_ptr[0] > 0) {;} // True Positive
  return 1;
}
