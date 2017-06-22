#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "content-gen.h"
#include <assert.h>


void generate_BLK_content_1(unsigned char buf[], unsigned char *content,
		unsigned int len, unsigned int targetlen)
{
	unsigned int donelen = 0;
	while (donelen < targetlen)
	{
		memcpy(buf+donelen, content, len);
		donelen += len;
	}
}

unsigned int get_seed_from_hash(unsigned char *content, unsigned int N)
{
	unsigned int sum = 0;
	unsigned int i;

	for (i=0;i < N; i++)
		sum += content[i];

	return sum;
}

/* generate_BLK_content_2: 
 *
 * N = 32, X = 4096
 *
 */
void generate_BLK_content_2(unsigned char buf[], unsigned char *content,
		unsigned int len, unsigned int targetlen)
{
	unsigned char tempbuf[len];
	unsigned int X = targetlen;
	unsigned int times;
	unsigned int N = len;
	unsigned int seed;
	int x, n;
	int i=0;	/* content iterator */
   	int	j=0;	/* target buf iterator */

	seed = get_seed_from_hash(content, N);

    /* Intializes random number generator */
    srand(seed);

	while (X > 0 && N > 0)
	{
		n = rand() % N + 1;	/*Pick random number [1:N]*/
		x = rand() % X + 1;	/*Pick random number [1:X]*/
		times = x / n;
		x = n * times;
		memset(tempbuf, 0, len);
		memcpy(tempbuf, content+i, n);
		generate_BLK_content_1(buf+j, tempbuf, n, x);

		i = i + n;
		j = j + x;
		N = N - n;
		X = X - x;
	}	
	if (X > 0)	/* Hash char over but buf[] still not full */
	{
		assert(N == 0);
		x = X;
		memset(tempbuf, 0, len);
		memcpy(tempbuf, content+len-1, 1);
		generate_BLK_content_1(buf+j, tempbuf, 1, x);
		j = j + x;
		X = X - x;
	}

	assert(X == 0);
}

void generate_BLK_content_3(unsigned char buf[], unsigned char *content,
		unsigned int len, unsigned int targetlen)
{
	unsigned char tempbuf[2];
	unsigned int X = targetlen;
	unsigned int N = len;
	unsigned int seed;
	int x;
	int i=0;
	int j=0;

	seed = get_seed_from_hash(content, N);

	/* Intializes random number generator */
	srand(seed);

	while (X > 0 && N > 0)
	{
		x = rand() % X + 1;
		memset(tempbuf, 0, 2);
		tempbuf[0] = content[i];
		tempbuf[1] = '\0';
		generate_BLK_content_1(buf+j, tempbuf, 1, x);
		j = j + x;
		X = X - x;
		i++;

	}
	if (X > 0)
	{
		assert(N == 0);
		x = X;
		tempbuf[0] = content[len-1];
		tempbuf[1] = '\0';
		generate_BLK_content_1(buf+j, tempbuf, 1, x);
		j = j + x;
		X = X - x;
	}

	assert(X == 0);
}

/* Same as generate_BLK_content_2, except that output content alphabet 
 * restricted to [0-9]
 */
void generate_BLK_content_4(unsigned char buf[], unsigned char *content,
		unsigned int len, unsigned int targetlen)
{
	//savemem unsigned char tempbuf[BLKSIZE];
	unsigned char* tempbuf = malloc(BLKSIZE);
	unsigned int X = targetlen;
	unsigned int times;
	unsigned int N = len;
	unsigned int seed;
	int x, n;
	int i=0;	/* content iterator */
   	int	j=0;	/* target buf iterator */
	unsigned char tempval = 0;
   	int	k=0;	/* tempbuf iterator */

	seed = get_seed_from_hash(content, N);

    /* Intializes random number generator */
    srand(seed);

	while (X > 0 && N > 0)
	{
		n = rand() % N + 1;	/*Pick random number [1:N]*/
		x = rand() % X + 1;	/*Pick random number [1:X]*/
		times = x / n;
		x = n * times;
		memset(tempbuf, 0, BLKSIZE);
//		memcpy(tempbuf, content+i, n);
		for (k=0; k<n; k++)
		{
			tempval = content[i+k];
			tempval = tempval % 5 + 48;
			tempbuf[k] = tempval;
		}
		generate_BLK_content_1(buf+j, tempbuf, n, x);

		i = i + n;
		j = j + x;
		N = N - n;
		X = X - x;
	}	
	if (X > 0)	/* Hash char over but buf[] still not full */
	{
		assert(N == 0);
		x = X;
		memset(tempbuf, 0, BLKSIZE);
		//memcpy(tempbuf, content+len-1, 1);
		for (k=0; k<x; k++)
		{
			tempval = content[len-1];
			tempval = tempval % 5 + 48;
			tempbuf[k] = tempval;
		}
		generate_BLK_content_1(buf+j, tempbuf, x, x);
		j = j + x;
		X = X - x;
	}

	assert(X == 0);
	free(tempbuf);
}

void generate_BLK_content_5(unsigned char buf[], unsigned char *content,
		unsigned int len, unsigned int targetlen)
{
	//savemem unsigned char tempbuf[BLKSIZE];
	unsigned char* tempbuf = malloc(BLKSIZE);
	unsigned int X = targetlen;
	unsigned int times;
	unsigned int N = len;
	unsigned int seed;
	int x, n;
	int i=0;	/* content iterator */
   	int	j=0;	/* target buf iterator */
	unsigned char tempval = 0;
   	int	k=0;	/* tempbuf iterator */

	seed = get_seed_from_hash(content, N);

    /* Intializes random number generator */
    srand(seed);

	while (X > 0 && N > 0)
	{
		n = rand() % N + 1;	/*Pick random number [1:N]*/
		x = rand() % X + 1;	/*Pick random number [1:X]*/
		times = x / n;
		x = n * times;
		memset(tempbuf, 0, BLKSIZE);
//		memcpy(tempbuf, content+i, n);
		for (k=0; k<n; k++)
		{
			tempval = content[i+k];
			tempval = tempval % 5 + 48;
			tempbuf[k] = tempval;
		}
		generate_BLK_content_1(buf+j, tempbuf, n, x);

		i = i + n;
		j = j + x;
		N = N - n;
		X = X - x;
	}	
	if (X > 0)	/* Hash char over but buf[] still not full */
	{
		assert(N == 0);
		x = X;
		memset(tempbuf, 0, BLKSIZE);
		//memcpy(tempbuf, content+len-1, 1);
		for (k=0; k<x; k++)
		{
			tempval = content[len-1];
			tempval = tempval % 5 + 48;
			tempbuf[k] = tempval;
		}
		generate_BLK_content_1(buf+j, tempbuf, x, x);
		j = j + x;
		X = X - x;
	}

	assert(X == 0);
	free(tempbuf);
}


void generate_BLK_content(unsigned char targetbuf[], unsigned char *content,
		unsigned int len, unsigned int targetlen)
{
//	fprintf(stdout, "In %s\n", __FUNCTION__);
	generate_BLK_content_4(targetbuf, content, len, targetlen);
	//generate_BLK_content_2(targetbuf, content, len, targetlen);
	//generate_BLK_content_3(targetbuf, content, len, targetlen);
}
