// RGB24_To_Y.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Shlwapi.h>

int main(int argc, char **argv)
{
	char *fname_major, *tmp;
	char fname_write[80];
	FILE *fp_r, *fp_w;;
	errno_t err;
	INT32 width, height;
	char *row_buf;
	int i, j;
	UINT8 R, G, B;
	UINT8 Y, U, V;

	if (argc < 2)
	{
		printf("Please input the RGB24 bitmap file name to convert.\n");
		return -1;
	}

	if ((err = fopen_s(&fp_r, argv[1], "rb")) != 0)
	{
		printf("open file %s failed\n", argv[1]);
		return -2;
	}
	else
	{
		printf("Open file \"%s\" ok to read:\n", argv[1]);
		fseek(fp_r, 18, SEEK_SET);
		fread_s(&width, 16, sizeof(INT32), 1, fp_r);
		fread_s(&height, 16, sizeof(INT32), 1, fp_r);
		height = height < 0 ? -height : height;
		printf("[width = %d, height = %d]\n", width, height);

		row_buf = (char *)calloc(width, 3);
		if (row_buf == NULL)
		{
			printf("buffer allocation failed\n");
			return -1;
		}
		fname_major = strtok_s(argv[1], ".", &tmp);
		if (fname_major)
		{
			strcpy_s(fname_write, fname_major);
			strcat_s(fname_write, ".yuv");
			if ((err = fopen_s(&fp_w, fname_write, "wb")) != 0)
			{
				printf("open file %s for writing failed\n", fname_write);
				return -1;
			}
		}
		else
			return -1;

		printf("Open file \"%s\" ok to write:\n", fname_write);
		fseek(fp_r, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), SEEK_SET);

		for (i = 0; i < height; i++)
		{
			if (fread_s(row_buf, (width * 3), sizeof(char), (width * 3), fp_r))
			{
				for (j = 0; j < (width * 3); j += 3)
				{
					R = row_buf[j];
					G = row_buf[j + 1];
					B = row_buf[j + 2];

					Y = (UINT8)(0.299*R + 0.587*G + 0.114*B);
					//U = 0;
					//V = 0;
					fwrite(&Y, sizeof(UINT8), 1, fp_w);
					//fwrite(&U, sizeof(UINT8), 1, fp_w);
					//fwrite(&V, sizeof(UINT8), 1, fp_w);
				}
			}

		}
		free(row_buf);
		fclose(fp_r);
		fclose(fp_w);
		printf("(Written Done)\n\n");
	}
	return 0;
}
