#include "network_numbers.hpp"
#include <cassert>
#include <iostream>
using tabremoto::bounded_unsigned;
int main(){unsigned n=0;
 assert(bounded_unsigned("65535",1,65535,n)&&n==65535);
 assert(!bounded_unsigned("65536",1,65535,n));
 assert(!bounded_unsigned("51821junk",1,65535,n));
 assert(!bounded_unsigned("-1",0,65535,n));
 assert(!bounded_unsigned("4294967296",0,65535,n));
 assert(!bounded_unsigned(std::string(8000,'9'),0,65535,n));
 assert(!bounded_unsigned("",0,65535,n));
 assert(bounded_unsigned("0",0,32,n)&&n==0);
 assert(bounded_unsigned("32",0,32,n)&&n==32);
 assert(!bounded_unsigned("33",0,32,n));
 assert(!bounded_unsigned("575",576,1420,n));
 assert(bounded_unsigned("1420",576,1420,n));
 std::cout<<"Network numeric limits passed\n";
}
