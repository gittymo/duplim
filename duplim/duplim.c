/*	duplim.c
		Duplicate image manager
		(C)2020 Morgan Evans */

#include <stdlib.h>
#include <stdio.h>
#include "jpeglib.h"

typedef struct {
	double Y;
	double Cr;
	double Cb;
} YCRCB_VALUE;

typedef struct {
	YCRCB_VALUE * values;
} YCRCB_LOOKUP;

typedef struct {
	YCRCB_VALUE ** blocks;
	int block_count;
	int width;
} MATCH_COLUMN;

typedef struct {
	char * source_path;
	MATCH_COLUMN ** columns;
	int column_count;
} MATCH_IMAGE;

typedef struct {
	YCRCB_LOOKUP * lum_lookup;
	YCRCB_LOOKUP * cr_lookup;
	YCRCB_LOOKUP * cb_lookup;
	int grid_divisions;
	int image_height;
	int threads;
} MATCH_CONFIG;

YCRCB_LOOKUP * createLookUp(const double RedConst, const double GreenConst, const double BlueConst) {
	YCRCB_LOOKUP * lookup = (YCRCB_LOOKUP *) malloc(sizeof(YCRCB_LOOKUP));
	lookup->values = (YCRCB_VALUE *) malloc(sizeof(YCRCB_VALUE) * 256);
	for (int i = 0; i < 256; i++) {
		lookup->values[i].Y		= RedConst * i;
		lookup->values[i].Cr 	= GreenConst * i;
		lookup->values[i].Cb	= BlueConst * i;
	}
}

void freeLookUp(YCRCB_LOOKUP * lookup) {
	if (lookup != NULL) {
		if (lookup->values != NULL) {
			free(lookup->values);
		}
		lookup->values = NULL;
		free(lookup);
	}
}

MATCH_CONFIG * createMatchingConfig(const double lum_red_const, const double lum_green_const, const double lum_blue_const,
																		const double cr_red_const, const double cr_green_const, const double cr_blue_const,
																		const double cb_red_const, const double cb_green_const, const double cb_blue_const,
																		const int match_grid_divisions, const int match_image_height, const int match_threads) {
	MATCH_CONFIG * config = (MATCH_CONFIG *) malloc(sizeof(MATCH_CONFIG));
	config->lum_lookup = createLookup(lum_red_const, lum_green_const, lum_blue_const);
	config->cr_lookup = createLookup(cr_red_const, cr_green_const, cr_blue_const);
	config->cb_lookup = createLookup(cb_red_const, cb_green_const, cb_blue_const);
	config->grid_divisions = (match_grid_divisions != 8 && match_grid_divisions != 32) ? 16 : match_grid_divisions;
	config->image_height = (match_image_height != 256 && match_image_height != 512) ? 128 : match_image_height;
	config->threads = (match_threads < 1) ? 1 : match_threads;

	return config;
}

void freeMatchingConfig(MATCH_CONFIG * cfg) {
	if (cfg != null) {
		freeLookup(cfg->lum_lookup);
		freeLookup(cfg->cr_lookup);
		freeLookup(cfg->cb_lookup);
		cfg->lum_lookup = cfg->cr_lookup = cfg->cb_lookup = NULL;
		cfg->grid_divisions = 0;
		cfg->image_height = 0;
		cfg->threads = 0;
		free(cfg);
	}
}

int getColumnCount(int image_width, int columns) {
	int trimWidth = (image_width / columns) * columns;
	return (trimWidth < image_width) ? columns + 1 : columns;
}

char * safestrcpy(const char * source, const int max_chars) {
	if (source == NULL || max_chars < 1) return NULL;
	int string_length = 0;
	while (source[string_length] != 0 && string_length < max_chars) string_length++;
	char * string_copy = (char *) malloc(sizeof(char) * (string_length + 1));
	int i;
	for (i = 0; i < string_length; i++) string_copy[i] = source[i];
	string_copy[i] = 0;

	return string_copy;
}

double getComponentValue(const int red, const int green, const int blue, MATCH_LOOKUP * lookup) {
	double result = 0;
	if (red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 && blue <= 255 && lookup != NULL) {
		result = lookup->values[red].Y + lookup->values[green].Cr + lookup->values[blue].Cb;
	}

	return result;
}

YCRCB_VALUE * createMatchBlock(unsigned char * samples, int x, int y, int column_width, int image_width, MATCH_CONFIG * cfg) {
	YCRCB_VALUE * block = NULL;

	if (y >= 0 && y <= cfg->image_height && cfg != NULL && y % cfg->grid_divisions == 0) {
		block = (YCRCB_VALUE *) malloc(sizeof(YCRCB_VALUE));
		const int origin = (3 * image_width * y) + (3 * x);
		const int dest = origin + (column_width * 3) + (3 * image_width * (cfg->image_height / cfg->grid_divisions));
		for (int scanlline = origin; scanline < dest; scanline += 3 * image_width) {
			for (int px = scanline; px < scanline + (column_width * 3); px += 3) {
				block->Y = getComponentValue(samples[px], samples[px + 1], samples[px + 2], cfg->lum_lookup);
				block->Cr = getComponentValue(samples[px], samples[px + 1], samples[px + 2], cfg->cr_lookup);
				block-CB = getComponentValue(samples[px], samples[px + 1], samples[px + 2], cfg->cb_lookup);
			}
		}
	}

	return block;
}

void freeMatchBlock(YCRCB_VALUE * block) {
	if (block != NULL) free(block);
}

MATCH_COLUMN * createMatchColumn(unsigned char * samples, int x, int column_width, int image_width, MATCH_CONFIG * cfg) {
	MATCH_COLUMN * mc = NULL;
	if (samples != NULL && x >=0 && x < image_width && column_width > 0 && column_width <= image_width && cfg != NULL) {
		mc = (MATCH_COLUMN *) malloc(sizeof(MATCH_COLUMN));
		mc->block_count = cfg->grid_divisions;
		mc->blocks = (YCRCB_VALUE **) malloc(sizeof(YCRCB_VALUE *) * cfg->grid_divisions);
		int y = 0;
		const int y_step = cfg->image_height / cfg->grid_divisions;
		for (int i = 0; i < cfg->grid_divisions; i++) {
			mc->blocks[i] = createMatchBlock(samples, x, y, column_width, image_wdith, cfg);
			y += y_step;
		}
		mc->width = column_width;
	}

	return mc;
}

void freeMatchColumn(MATCH_COLUMN * mc) {
	if (mc != NULL) {
		for (int i = 0; i < 16; i++) {
			free(mc->blocks[i]);
			mc->blocks[i] = NULL;
		}
		free(mc->blocks);
		mc->block_count = 0;
		mc->width = 0;
		free(mc);
	}
}

MATCH_IMAGE * createMatchImage(const char * filename, unsigned char * samples, int image_width, MATCH_CONFIG * cfg) {
	if (cfg == NULL || filename == NULL || samples == NULL || image_width < 128) return NULL;

	const int MATCH_COLUMN_COUNT = getColumnCount(image_width, cfg->grid_divisions);
	const int COLUMN_WIDTH = image_width / cfg->grid_divisions;
	MATCH_IMAGE * mi = (MATCH_IMAGE *) malloc(sizeof(MATCH_IMAGE));
	mi->column_count = MATCH_COLUMN_COUNT;
	mi->source_path = safestrcpy(filename, 255);
	mi->columns = (MATCH_COLUMN **) malloc(sizeof(MATCH_COLUMN *) * MATCH_COLUMN_COUNT);
	int i = 0;
	for (int x = 0; x < image_width; x += COLUMN_WIDTH) {
		mi->columns[i++] = createMatchColumn(samples, x,
			(x + COLUMN_WIDTH <= image_width) ? COLUMN_WIDTH : image_width - x, image_width, cfg);
	}
	return mi;
}

void freeMatchImage(MATCH_IMAGE * mi) {
	if (mi != NULL) {
		free(mi->source_path);
		mi->source_path = NULL;
		for (int i = 0; i < mi->column_count; i++) {
			freeMatchColumn(mi->columns[i]);
			mi->columns[i] = NULL;
		}
		free(mi->columns);
		mi->columns = NULL;
		mi->column_count = 0;
		free(mi);
	}
}

MATCH_IMAGE * loadJPEG(const char * filename, MATCH_CONFIG * cfg) {
	MATCH_IMAGE * img = NULL;
	if (filename != null && cfg != null) {
		struct jpeg_decompress_struct cinfo;
		struct jpeg_error_mgr jerr;
		FILE * f = fopen(filename, "rb");
		if (f != NULL) {
			jpeg_stdio_src(&cinfo, f);
			jpeg_read_header(&cinfo, TRUE);
			cinfo.output_height = cfg->image_height;
			jpeg_start_decompress(&cinfo);
			const int ROW_STRIDE = cinfo.output_width * cinfo.output_components;
			unsigned char * samples = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, ROW_STRIDE, cinfo.output_height);
			jpeg_read_scanlines(&cinfo, (JSAMPARRAY) samples, cinfo.output_height);
			img = createMatchImage(filename, samples, cinfo.output_width, cfg);
			jpeg_finish_decompress(&cinfo);
			jpeg_destroy_decompress(&cinfo);
			fclose(f);
		}
	}

	return img;
}

int main(int argc, char ** argv) {
	// The RGB to YCrCb conversion table.
	
	MATCH_CONFIG * cfg = createMatchingConfig(0.299, 0.587, 0.114, -0.147, -0.209, 0.436, 0.615, -0.515, -0.1, 16, 128, 1);

}
