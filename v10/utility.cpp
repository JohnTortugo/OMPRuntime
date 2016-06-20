#include <stdio.h>


void print_num_in_chars (long long unsigned int num)
{
	for (int i = 0; i < 8; i++)
	{
		printf("%c", (unsigned char)(num >> (7 - i) * 8) & 0xFF);
	}
	//printf("\n");
	fflush(stdout);
}
