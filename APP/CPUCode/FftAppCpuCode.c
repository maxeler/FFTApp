#include <math.h>
#include <complex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "MaxSLiCInterface.h"
#include "Maxfiles.h"

/**
 * Wrapper for malloc in order to check if malloc returns NULL and exit if this happens.
 *
 * @param size Size of the memory to allocate.
 * @return pointer to the allocated memory.
 */
void * mallocWrapper(size_t size) {
	void * pointer = malloc(size);
	if (pointer == NULL) {
		fprintf(stderr, "Was not able to allocate enough memory");
		exit(-1);
	}
	return pointer;
}

/**
 * Compares the result of the fft calculated by the dfe with the result of the cpu version.
 * The signal to noise ratio gets calculated and if it is to low the actual results as well
 * as the expected results get printed.
 *
 * @param size Number of samples.
 * @param expected Expected result.
 * @param result Actual result.
 * @return 0 if SNR is ok. 1 if not.
 */
int check(const int size, const float complex *expected, const float complex *result)
{
	// calculate SNR
	double S = 0.0;
	double N = 0.0;

	for (int i = 0; i < size; i++) {
		float complex res = result[i];
		float complex exp = expected[i];

		float complex err = exp - res;

		S += (creal(exp) * creal(exp)) + (cimag(exp) * cimag(exp));
		N += (creal(err) * creal(err)) + (cimag(err) * cimag(err));
	}

	double SNR = (10 * log10(S / N));
	printf("SNR: %f\n", SNR);
	// if result is wrong print everything
	if (SNR < 69.0) {
		for (int i = 0; i < size; i++) {
			float complex res = result[i];
			float complex exp = expected[i];

			printf("Index %d: Is: %f + %f * i\tExpected: %f + %f * i\n", i, creal(res), cimag(res),
					creal(exp), cimag(exp));
		}
	}

	return SNR < 69.0;
}

/**
 * randomly generate data for the fft.
 *
 * @param size Number of samples.
 * @param data Pointer to the array used to store the data.
 */
void generateTestData(const int size, float complex *data) {
	srand(time(NULL));
	for (int i = 0; i < size; i++) {
		float real = (float)rand()/(float)RAND_MAX * 10;
		float imag = (float)rand()/(float)RAND_MAX * 10;
		int signReal = rand() % 2;
		int signImag = rand() % 2;

		real = signReal == 0 ? real : -real;
		imag = signImag == 0 ? imag : -imag;

		data[i] = real + I * imag;
		data[i] = i;
	}
}

/**
 * CPU code to calculate a 1D fft.
 * This function is not written to be fast. Only to be simple.
 *
 * @param n Size of the fft (has to be power of 2).
 * @param values Input samples. Also used to store the coefficients.
 */
void fftCPU(const int n, float complex* values) {
	if (n > 1) {
		float complex *g = (float complex*) mallocWrapper((n / 2) * sizeof(float complex));
		float complex *u = (float complex*) mallocWrapper((n / 2) * sizeof(float complex));


		// reorder data
		for (int i = 0; i < n / 2; i++) {
			g[i] = values[i * 2];
			u[i] = values[i * 2 + 1];
		}

		// calculate fft recursively
		fftCPU(n / 2, g);
		fftCPU(n / 2, u);

		// combine results
		for (int k = 0; k < n / 2; k++) {
			float complex expFactor = cexpf(-2.0 * M_PI * I * (float complex) k / (float complex) n);
			values[k]         = g[k] + u[k] * expFactor;
			values[k + n / 2] = g[k] - u[k] * expFactor;
		}

		// free allocated memory
		free(g);
		free(u);
	}
}

/**
 * Function to transpose a 2D Array.
 *
 * @param firstDimension Size of the first dimension.
 * @param secondDimension Size of the second dimension.
 * @param data The array to transpose. It will be replaced by the transposed array.
 */
void transposeData(const int firstDimension, const int secondDimension, float complex*** data) {
	float complex** newData = mallocWrapper(firstDimension * sizeof(float complex*));
	for (int i = 0; i < firstDimension; i++) {
		newData[i] = mallocWrapper(secondDimension * sizeof(float complex));
		for (int j = 0; j < secondDimension; j++) {
			newData[i][j] = (*data)[j][i];
		}
	}

	// free space again
	for (int i = 0; i < secondDimension; i++) {
		free((*data)[i]);
	}
	free(*data);
	*data = newData;
}

/**
 * Wrapper function for the CPU implementation of the 1D fft.
 * In the 1D case the input data gets copied into a new array in order to do not change the input.
 * In the 2D case the data has to be reordered, transposed and multiple 1D ffts have to be executed.
 * The same principle applies for the 3D case
 *
 * @param size number of samples.
 * @param inputData Sample array.
 * @param expectedData Array for the coefficients.
 */
void fftCPUWrapper(const int size, float complex* inputData, float complex* expectedData) {
	if (FftApp_M == 1) { // 1D FFT
		memcpy((void*) expectedData, (void*) inputData, size * sizeof(float complex));
		fftCPU(size, expectedData);
	} else if (FftApp_L == 1) { // 2D FFT
		// To make things clearer again we copy our data into a two dimensional array
		float complex** inputData2D = mallocWrapper(FftApp_M * sizeof(float complex*));
		for (int i = 0; i < FftApp_M; i++) {
			inputData2D[i] = mallocWrapper(FftApp_N * sizeof(float complex));
			for (int j = 0; j < FftApp_N; j++) {
				inputData2D[i][j] = inputData[i * FftApp_N + j];
			}
		}

		// Calculate fft on each row
		for (int i = 0; i < FftApp_M; i++) {
			fftCPU(FftApp_N, inputData2D[i]);
		}

		// transpose
		transposeData(FftApp_N, FftApp_M, &inputData2D);

		// calculate fft on each column
		for (int i = 0; i < FftApp_N; i++) {
			fftCPU(FftApp_M, inputData2D[i]);
		}

		// transpose back
		transposeData(FftApp_M, FftApp_N, &inputData2D);

		for (int i = 0; i < FftApp_M; i++) {
			for (int j = 0; j < FftApp_N; j++) {
				expectedData[i * FftApp_N + j] = inputData2D[i][j];
			}
		}

		for (int i = 0; i < FftApp_M; i++) {
			free(inputData2D[i]);
		}
		free(inputData2D);
	} else { // 3D FFT
		// To make things clearer again we copy our data into a three dimensional Array
		float complex*** inputData3D = mallocWrapper(FftApp_L * sizeof(float complex**));
		for (int i = 0; i < FftApp_L; i++) {
			inputData3D[i] = mallocWrapper(FftApp_M * sizeof(float complex*));
			for (int j = 0; j < FftApp_M; j++) {
				inputData3D[i][j] = mallocWrapper(FftApp_N * sizeof(float complex));
				for (int k = 0; k < FftApp_N; k++) {
					inputData3D[i][j][k] = inputData[i * FftApp_N * FftApp_M + j * FftApp_N + k];
				}
			}
		}

		// Now we can first calculate the fft on each row
		for (int i = 0; i < FftApp_L; i++) {
			for (int j = 0; j < FftApp_M; j++) {
				fftCPU(FftApp_N, inputData3D[i][j]);
			}
		}

		// Transpose N and M Dimension
		for (int i = 0; i < FftApp_L; i++) {
			transposeData(FftApp_N, FftApp_M, &(inputData3D[i]));
		}

		// Calculate fft on each column
		for (int i = 0; i < FftApp_L; i++) {
			for (int j = 0; j < FftApp_N; j++) {
				fftCPU(FftApp_M, inputData3D[i][j]);
			}
		}

		// Transpose N and M Dimension back
		for (int i = 0; i < FftApp_L; i++) {
			transposeData(FftApp_M, FftApp_N, &(inputData3D[i]));
		}

		// Now do fft in the L Dimension
		float complex* buffer = mallocWrapper(FftApp_L * sizeof(float complex));
		for (int i = 0; i < FftApp_M; i++) {
			for (int j = 0; j < FftApp_N; j++) {
				for (int k = 0; k < FftApp_L; k++) {
					buffer[k] = inputData3D[k][i][j];
				}
				fftCPU(FftApp_L, buffer);
				for (int k = 0; k < FftApp_L; k++) {
					inputData3D[k][i][j] = buffer[k];
				}
			}
		}
		free(buffer);

		for (int i = 0; i < FftApp_L; i++) {
			for (int j = 0; j < FftApp_M; j++) {
				for (int k = 0; k < FftApp_N; k++) {
					expectedData[i * FftApp_N * FftApp_M + j * FftApp_N + k] = inputData3D[i][j][k];
				}
			}
		}

		for (int i = 0; i < FftApp_L; i++) {
			for (int j = 0; j < FftApp_M; j++) {
				free(inputData3D[i][j]);
			}
			free(inputData3D[i]);
		}
		free(inputData3D);
	}
}

/**
 * Code to run the fft on the DFE.
 *
 * @param size Number of samples.
 * @param input Array of samples.
 * @param result Array for the coefficients.
 */
void fftDFE(const int size, float complex* input, float complex* result) {
	printf("Running on DFE.\n");
	FftApp(size / 4, input, size * sizeof(float complex), result, size * sizeof(float complex));
}


int main(void)
{
	const int size = FftApp_N * FftApp_M * FftApp_L;
	float complex* inputData = mallocWrapper(size * sizeof(float complex));
	float complex* expectedData = mallocWrapper(size * sizeof(float complex));
	float complex* resultData = mallocWrapper(size * sizeof(float complex));

	generateTestData(size, inputData);

	fftCPUWrapper(size, inputData, expectedData);

	fftDFE(size, inputData, resultData);

	int status = check(size, expectedData, resultData);
	if(status != 0)
		printf("Test failed!\n");
	else
		printf("Test passed!\n");

	printf("Done.\n");

	free(inputData);
	free(expectedData);
	free(resultData);
	return status;
}
