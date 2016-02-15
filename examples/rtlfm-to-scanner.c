/* Copyright (c) 2016 Kester Everts
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <unistd.h>
#include <string.h>

#define SAMPLES_PER_BIT (15)
#define BYTES_PER_SAMPLE (2)
#define BIT_SAMPLE_SIZE (BYTES_PER_SAMPLE * SAMPLES_PER_BIT)

#define OUTPUT_BUFFER_SIZE (200)

static char buffer[BIT_SAMPLE_SIZE * 2048];
static char residualBuffer[BIT_SAMPLE_SIZE];
static size_t residualBufferLength = 0;

static char outputByte = 0;
static int outputByteBitPos = 0;

static char outputBuffer[OUTPUT_BUFFER_SIZE];
static size_t outputBufferPos = 0;

static const char STR_DONE[] = "Processing done!\n";

void processOutput() {
	write(STDOUT_FILENO, outputBuffer, sizeof(outputBuffer));
	outputBufferPos = 0;
}

void processByte() {
	outputBuffer[outputBufferPos++] = outputByte;
	outputByte = 0;
	outputByteBitPos = 0;

	if(outputBufferPos == sizeof(outputBuffer)) {
		processOutput();
	}
}

void processBit(const char* buffer) {
	int sampleSum = 0;
	for(size_t i = 0; i < BIT_SAMPLE_SIZE; i += BYTES_PER_SAMPLE) {
		char highestSampleByte = buffer[i + BYTES_PER_SAMPLE - 1];
		if(highestSampleByte & 0x80) {
			sampleSum--;
		} else {
			sampleSum++;
		}
	}
	if(sampleSum > 0) {
		outputByte |= 1 << (7 - outputByteBitPos);
	}

	outputByteBitPos++;

	if(outputByteBitPos == 8) {
		processByte();
	}
}

int main(int argc, const char* argv[]) {
	ssize_t bytesRead;
	while((bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
		size_t pos = 0;
		if(residualBufferLength > 0) {
			if(residualBufferLength + bytesRead < BIT_SAMPLE_SIZE) {
				memcpy(residualBuffer + residualBufferLength, buffer, bytesRead);
				residualBufferLength += bytesRead;
				continue;
			}
			size_t readFromBuffer = BIT_SAMPLE_SIZE - residualBufferLength;
			memcpy(residualBuffer + residualBufferLength, buffer, readFromBuffer);
			processBit(residualBuffer);
			pos = readFromBuffer;
		}

		while(pos + BIT_SAMPLE_SIZE < bytesRead) {
			processBit(buffer + pos);
			pos += BIT_SAMPLE_SIZE;
		}

		if(pos < bytesRead) {
			residualBufferLength = bytesRead - pos;
			memcpy(residualBuffer, buffer + pos, residualBufferLength);
		} else {
			residualBufferLength = 0;
		}
	}
	if(bytesRead == 0) {
		// flush the output buffer
		if(outputBufferPos > 0) {
			write(STDOUT_FILENO, outputBuffer, outputBufferPos);
		}
		write(STDERR_FILENO, STR_DONE, sizeof(STR_DONE));
	}
}
