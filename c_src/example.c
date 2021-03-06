#include "example.h"

/* A global variable */
double Foo = 3.0;

float pie() {
  return 3.14;
}

bool prime(int n) {
  int i;
  for (i = 2; i < n/2; i++) {
    if (n%i == 0) return false;
  }
  return true;
}

/* Compute the greatest common divisor of positive integers */
int gcd(int x, int y) {
  int g;
  g = y;
  while (x > 0) {
    g = x;
    x = y % x;
    y = g;
  }
  return g;
}
