/**
 * @file gl_image.c
 * @brief
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "gl_local.h"

static char glerrortex[MAX_GLERRORTEX];
static char *glerrortexend;
static image_t gltextures[MAX_GLTEXTURES];
static int numgltextures;

static byte intensitytable[256];
static byte gammatable[256];

cvar_t *gl_intensity;
extern cvar_t *gl_imagefilter;

unsigned d_8to24table[256];

static qboolean GL_Upload8(byte * data, int width, int height, qboolean mipmap, imagetype_t type, image_t* image);
static qboolean GL_Upload32(unsigned *data, int width, int height, qboolean mipmap, qboolean clamp, imagetype_t type, image_t* image);

int gl_solid_format = GL_RGB;
int gl_alpha_format = GL_RGBA;

int gl_compressed_solid_format = 0;
int gl_compressed_alpha_format = 0;

int gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int gl_filter_max = GL_LINEAR;

#if 0
/**
 * @brief Set the anisotropic value for textures
 * @note not used atm
 * @sa GL_ResampleTexture
 */
extern void GL_UpdateAnisotropy (void)
{
	int		i;
	image_t	*glt;
	float	value;

	if (!gl_state.anisotropic)
		value = 0;
	else
		value = r_ext_max_anisotropy->value;

	for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
		if (glt->type != it_pic) {
			GL_Bind(glt->texnum);
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, value);
		}
	}
}
#endif

/**
 * @brief
 */
extern void GL_EnableMultitexture (qboolean enable)
{
	if (!qglSelectTextureSGIS && !qglActiveTextureARB)
		return;

	if (enable) {
		GL_SelectTexture(gl_texture1);
		qglEnable(GL_TEXTURE_2D);
		GL_TexEnv(GL_REPLACE);
	} else {
		GL_SelectTexture(gl_texture1);
		qglDisable(GL_TEXTURE_2D);
		GL_TexEnv(GL_REPLACE);
	}
	GL_SelectTexture(gl_texture0);
	GL_TexEnv(GL_REPLACE);
}

/**
 * @brief
 */
extern void GL_SelectTexture (GLenum texture)
{
	int tmu;

	if (!qglSelectTextureSGIS && !qglActiveTextureARB)
		return;

	if (texture == gl_texture0)
		tmu = 0;
	else
		tmu = 1;

	if (tmu == gl_state.currenttmu)
		return;

	gl_state.currenttmu = tmu;

	if (qglSelectTextureSGIS) {
		qglSelectTextureSGIS(texture);
	} else if (qglActiveTextureARB) {
		qglActiveTextureARB(texture);
		qglClientActiveTextureARB(texture);
	}
}

/**
 * @brief
 */
extern void GL_TexEnv (GLenum mode)
{
	static GLenum lastmodes[2] = { (GLenum) - 1, (GLenum) - 1 };

	if (mode != lastmodes[gl_state.currenttmu]) {
		qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
		lastmodes[gl_state.currenttmu] = mode;
	}
}

/**
 * @brief
 */
extern void GL_Bind (int texnum)
{
	if (gl_state.currenttextures[gl_state.currenttmu] == texnum)
		return;
	gl_state.currenttextures[gl_state.currenttmu] = texnum;
	qglBindTexture(GL_TEXTURE_2D, texnum);
}

/**
 * @brief
 */
extern void GL_MBind (GLenum target, int texnum)
{
	GL_SelectTexture(target);
	if (target == gl_texture0) {
		if (gl_state.currenttextures[0] == texnum)
			return;
	} else {
		if (gl_state.currenttextures[1] == texnum)
			return;
	}
	GL_Bind(texnum);
}

typedef struct {
	char *name;
	int minimize, maximize;
} glmode_t;

static const glmode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

#define NUM_GL_MODES (sizeof(modes) / sizeof (glmode_t))

typedef struct {
	char *name;
	int mode;
} gltmode_t;

static const gltmode_t gl_alpha_modes[] = {
	{"default", 4},
	{"GL_RGBA", GL_RGBA},
	{"GL_RGBA8", GL_RGBA8},
	{"GL_RGB5_A1", GL_RGB5_A1},
	{"GL_RGBA4", GL_RGBA4},
	{"GL_RGBA2", GL_RGBA2},
};

#define NUM_GL_ALPHA_MODES (sizeof(gl_alpha_modes) / sizeof (gltmode_t))

static const gltmode_t gl_solid_modes[] = {
	{"default", 3},
	{"GL_RGB", GL_RGB},
	{"GL_RGB8", GL_RGB8},
	{"GL_RGB5", GL_RGB5},
	{"GL_RGB4", GL_RGB4},
	{"GL_R3_G3_B2", GL_R3_G3_B2},
#ifdef GL_RGB2_EXT
	{"GL_RGB2", GL_RGB2_EXT},
#endif
};

#define NUM_GL_SOLID_MODES (sizeof(gl_solid_modes) / sizeof (gltmode_t))

/**
 * @brief
 */
extern void GL_TextureMode (const char *string)
{
	int i;
	image_t *glt;

	for (i = 0; i < NUM_GL_MODES; i++) {
		if (!Q_stricmp(modes[i].name, string))
			break;
	}

	if (i == NUM_GL_MODES) {
		ri.Con_Printf(PRINT_ALL, "bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	/* change all the existing mipmap texture objects */
	for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
		if (glt->type != it_pic) {
			GL_Bind(glt->texnum);
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		}
	}
}

/**
 * @brief
 */
extern void GL_TextureAlphaMode (const char *string)
{
	int i;

	for (i = 0; i < NUM_GL_ALPHA_MODES; i++) {
		if (!Q_stricmp(gl_alpha_modes[i].name, string))
			break;
	}

	if (i == NUM_GL_ALPHA_MODES) {
		ri.Con_Printf(PRINT_ALL, "bad alpha texture mode name\n");
		return;
	}

	gl_alpha_format = gl_alpha_modes[i].mode;
}

/**
 * @brief
 */
extern void GL_TextureSolidMode (const char *string)
{
	int i;

	for (i = 0; i < NUM_GL_SOLID_MODES; i++) {
		if (!Q_stricmp(gl_solid_modes[i].name, string))
			break;
	}

	if (i == NUM_GL_SOLID_MODES) {
		ri.Con_Printf(PRINT_ALL, "bad solid texture mode name\n");
		return;
	}

	gl_solid_format = gl_solid_modes[i].mode;
}

/**
 * @brief Shows all loaded images
 */
extern void GL_ImageList_f (void)
{
	int i;
	image_t *image;
	int texels;

	ri.Con_Printf(PRINT_ALL, "------------------\n");
	texels = 0;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++) {
		if (image->texnum <= 0)
			continue;
		texels += image->upload_width * image->upload_height;
		switch (image->type) {
		case it_skin:
			ri.Con_Printf(PRINT_ALL, "M");
			break;
		case it_sprite:
			ri.Con_Printf(PRINT_ALL, "S");
			break;
		case it_wall:
			ri.Con_Printf(PRINT_ALL, "W");
			break;
		case it_pic:
			ri.Con_Printf(PRINT_ALL, "P");
			break;
		default:
			ri.Con_Printf(PRINT_ALL, " ");
			break;
		}

		ri.Con_Printf(PRINT_ALL, " %3i %3i RGB: %s - shader: %s\n",
				image->upload_width, image->upload_height, image->name,
				(image->shader ? image->shader->name : "NONE"));
	}
	ri.Con_Printf(PRINT_ALL, "Total textures: %i (max textures: %i)\n", numgltextures, MAX_GLTEXTURES);
	ri.Con_Printf(PRINT_ALL, "Total texel count (not counting mipmaps): %i\n", texels);
}


/*
=============================================================================
scrap allocation

Allocate all the little status bar objects into a single texture
to crutch up inefficient hardware / drivers
=============================================================================
*/

#define	MAX_SCRAPS		1
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

static int scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
static byte scrap_texels[MAX_SCRAPS][BLOCK_WIDTH * BLOCK_HEIGHT];
qboolean scrap_dirty;

/**
 * @brief
 * @return a texture number and the position inside it
 */
static int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int i, j;
	int best, best2;
	int texnum;

	for (texnum = 0; texnum < MAX_SCRAPS; texnum++) {
		best = BLOCK_HEIGHT;

		for (i = 0; i < BLOCK_WIDTH - w; i++) {
			best2 = 0;

			for (j = 0; j < w; j++) {
				if (scrap_allocated[texnum][i + j] >= best)
					break;
				if (scrap_allocated[texnum][i + j] > best2)
					best2 = scrap_allocated[texnum][i + j];
			}
			if (j == w) {		/* this is a valid spot */
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	return -1;
/*	Sys_Error ("Scrap_AllocBlock: full"); */
}

static int scrap_uploads = 0;

/**
 * @brief
 */
extern void Scrap_Upload (void)
{
	scrap_uploads++;
	GL_Bind(TEXNUM_SCRAPS);
	GL_Upload8(scrap_texels[0], BLOCK_WIDTH, BLOCK_HEIGHT, qfalse, it_pic, NULL);
	scrap_dirty = qfalse;
}

/*
=================================================================
PCX LOADING
=================================================================
*/


/**
 * @brief
 * @sa LoadTGA
 * @sa LoadJPG
 * @sa LoadPNG
 * @sa GL_FindImage
 */
static void LoadPCX (const char *filename, byte ** pic, byte ** palette, int *width, int *height)
{
	byte *raw;
	pcx_t *pcx;
	int x, y;
	int len;
	int dataByte, runLength;
	byte *out, *pix;

	if (*pic != NULL)
		Sys_Error("possible mem leak in LoadPCX\n");
	*palette = NULL;

	/* load the file */
	len = ri.FS_LoadFile(filename, (void **) &raw);
	if (!raw) {
		ri.Con_Printf(PRINT_DEVELOPER, "LoadPCX: Could not load pcx file '%s'\n", filename);
		return;
	}

	/* parse the PCX file */
	pcx = (pcx_t *) raw;

	pcx->xmin = LittleShort(pcx->xmin);
	pcx->ymin = LittleShort(pcx->ymin);
	pcx->xmax = LittleShort(pcx->xmax);
	pcx->ymax = LittleShort(pcx->ymax);
	pcx->hres = LittleShort(pcx->hres);
	pcx->vres = LittleShort(pcx->vres);
	pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
	pcx->palette_type = LittleShort(pcx->palette_type);

	raw = &pcx->data;

	if (pcx->manufacturer != 0x0a || pcx->version != 5 || pcx->encoding != 1 || pcx->bits_per_pixel != 8 || pcx->xmax >= 640 || pcx->ymax >= 480) {
		ri.Con_Printf(PRINT_ALL, "LoadPCX: Bad pcx file %s\n", filename);
		ri.Con_Printf(PRINT_ALL, "manufacturer: %x, version: %i, encoding: %i, bits_per_pixel: %i, xmax: %i, ymax: %i\n",
			pcx->manufacturer,
			pcx->version,
			pcx->encoding,
			pcx->bits_per_pixel,
			pcx->xmax,
			pcx->ymax);
		ri.FS_FreeFile(raw);
		return;
	}

	out = ri.TagMalloc(ri.imagePool, (pcx->ymax + 1) * (pcx->xmax + 1), 0);
	if (!out) {
		ri.Sys_Error(ERR_FATAL, "TagMalloc: failed on allocation of %i bytes", (pcx->ymax + 1) * (pcx->xmax + 1));
		return;					/* never reached. need for code analyst. */
	}

	*pic = out;

	pix = out;

	if (palette) {
		*palette = ri.TagMalloc(ri.imagePool, 768, 0);;
		memcpy(*palette, (byte *) pcx + len - 768, 768);
	}

	if (width)
		*width = pcx->xmax + 1;
	if (height)
		*height = pcx->ymax + 1;

	for (y = 0; y <= pcx->ymax; y++, pix += pcx->xmax + 1) {
		for (x = 0; x <= pcx->xmax;) {
			dataByte = *raw++;

			if ((dataByte & 0xC0) == 0xC0) {
				runLength = dataByte & 0x3F;
				dataByte = *raw++;
			} else
				runLength = 1;

			while (runLength-- > 0)
				pix[x++] = dataByte;
		}
	}

	if (raw - (byte *) pcx > len) {
		ri.Con_Printf(PRINT_DEVELOPER, "PCX file %s was malformed", filename);
		ri.TagFree(out);

		if (pic)
			*pic = NULL;
		if (palette) {
			ri.TagFree(palette);
			*palette = NULL;
		}
	}

	ri.FS_FreeFile(pcx);
}

/*
==============================================================================
PNG LOADING
==============================================================================
*/

typedef struct pngBuf_s {
	byte	*buffer;
	size_t	pos;
} pngBuf_t;

static void PngReadFunc (png_struct *Png, png_bytep buf, png_size_t size)
{
	pngBuf_t *PngFileBuffer = (pngBuf_t*)png_get_io_ptr(Png);
	memcpy (buf,PngFileBuffer->buffer + PngFileBuffer->pos, size);
	PngFileBuffer->pos += size;
}


/**
 * @brief
 * @sa LoadPCX
 * @sa LoadTGA
 * @sa LoadJPG
 * @sa GL_FindImage
 */
static int LoadPNG (const char *name, byte **pic, int *width, int *height)
{
	int				rowptr;
	int				samples, color_type, bit_depth;
	png_structp		png_ptr;
	png_infop		info_ptr;
	png_infop		end_info;
	byte			**row_pointers;
	byte			*img;
	uint32_t		i;

	pngBuf_t		PngFileBuffer = {NULL,0};

	if (*pic != NULL)
		Sys_Error("possible mem leak in LoadPNG\n");

	/* Load the file */
	ri.FS_LoadFile(name, (void **)&PngFileBuffer.buffer);
	if (!PngFileBuffer.buffer)
		return 0;

	/* Parse the PNG file */
	if ((png_check_sig(PngFileBuffer.buffer, 8)) == 0) {
		Com_Printf("LoadPNG: Not a PNG file: %s\n", name);
		ri.FS_FreeFile(PngFileBuffer.buffer);
		return 0;
	}

	PngFileBuffer.pos = 0;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,  NULL, NULL);
	if (!png_ptr) {
		Com_Printf("LoadPNG: Bad PNG file: %s\n", name);
		ri.FS_FreeFile(PngFileBuffer.buffer);
		return 0;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		Com_Printf("LoadPNG: Bad PNG file: %s\n", name);
		ri.FS_FreeFile(PngFileBuffer.buffer);
		return 0;
	}

	end_info = png_create_info_struct (png_ptr);
	if (!end_info) {
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		Com_Printf("LoadPNG: Bad PNG file: %s\n", name);
		ri.FS_FreeFile(PngFileBuffer.buffer);
		return 0;
	}

	/* get some usefull information from header */
	bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	color_type = png_get_color_type(png_ptr, info_ptr);

	/**
	 * we want to treat all images the same way
	 * The following code transforms grayscale images of less than 8 to 8 bits,
	 * changes paletted images to RGB, and adds a full alpha channel if there is
	 * transparency information in a tRNS chunk.
	 */

	/* convert index color images to RGB images */
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);
	/* convert 1-2-4 bits grayscale images to 8 bits grayscale */
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_gray_1_2_4_to_8(png_ptr);
	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);

	if (bit_depth == 16)
		png_set_strip_16(png_ptr);
	else if (bit_depth < 8)
		png_set_packing(png_ptr);

	png_set_read_fn(png_ptr, (png_voidp)&PngFileBuffer, (png_rw_ptr)PngReadFunc);

	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	row_pointers = png_get_rows(png_ptr, info_ptr);

	rowptr = 0;


	img = ri.TagMalloc(ri.imagePool, info_ptr->width * info_ptr->height * 4, 0);
	if (pic)
		*pic = img;

	if (info_ptr->channels == 4) {
		for (i = 0; i < info_ptr->height; i++) {
			memcpy (img + rowptr, row_pointers[i], info_ptr->rowbytes);
			rowptr += info_ptr->rowbytes;
		}
	} else {
		uint32_t	j;

		memset(img, 255, info_ptr->width * info_ptr->height * 4);
		for (rowptr = 0, i = 0; i < info_ptr->height; i++) {
			for (j = 0; j < info_ptr->rowbytes; j += info_ptr->channels) {
				memcpy(img + rowptr, row_pointers[i] + j, info_ptr->channels);
				rowptr += 4;
			}
		}
	}

	if (width)
		*width = info_ptr->width;
	if (height)
		*height = info_ptr->height;
	samples = info_ptr->channels;

	png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);

	ri.FS_FreeFile(PngFileBuffer.buffer);
	return samples;
}

/**
 * @brief
 * @sa LoadTGA
 * @sa LoadJPG
 * @sa LoadPCX
 * @sa GL_FindImage
 */
extern void WritePNG (FILE *f, byte *buffer, int width, int height)
{
	int			i;
	png_structp	png_ptr;
	png_infop	info_ptr;
	png_bytep	*row_pointers;

	png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		Com_Printf("WritePNG: LibPNG Error!\n");
		return;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct (&png_ptr, (png_infopp)NULL);
		Com_Printf("WritePNG: LibPNG Error!\n");
		return;
	}

	png_init_io (png_ptr, f);

	png_set_IHDR (png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_set_compression_level (png_ptr, Z_DEFAULT_COMPRESSION);
	png_set_compression_mem_level (png_ptr, 9);

	png_write_info (png_ptr, info_ptr);

	row_pointers = ri.TagMalloc(ri.imagePool, height * sizeof(png_bytep), 0);
	for (i = 0; i < height; i++)
		row_pointers[i] = buffer + (height - 1 - i) * 3 * width;

	png_write_image(png_ptr, row_pointers);
	png_write_end(png_ptr, info_ptr);

	png_destroy_write_struct(&png_ptr, &info_ptr);

	ri.TagFree(row_pointers);
}

/*
=========================================================
TARGA LOADING
=========================================================
*/

typedef struct _TargaHeader {
	unsigned char id_length, colormap_type, image_type;
	unsigned short colormap_index, colormap_length;
	unsigned char colormap_size;
	unsigned short x_origin, y_origin, width, height;
	unsigned char pixel_size, attributes;
} TargaHeader;

/*
#define TGA_COLMAP_UNCOMP		1
#define TGA_COLMAP_COMP			9
*/
#define TGA_UNMAP_UNCOMP		2
#define TGA_UNMAP_COMP			10

/**
 * @brief
 * @sa LoadPCX
 * @sa LoadJPG
 * @sa LoadPNG
 * @sa GL_FindImage
 */
extern void LoadTGA (const char *name, byte ** pic, int *width, int *height)
{
	int columns, rows, numPixels;
	byte *pixbuf;
	int row, column;
	byte *buf_p;
	byte *buffer;
	int length;
	TargaHeader targa_header;
	byte *targa_rgba;
	byte tmp[2];

	if (*pic != NULL)
		Sys_Error("possible mem leak in LoadTGA\n");

	/* load the file */
	length = ri.FS_LoadFile(name, (void **) &buffer);
	if (!buffer) {
		ri.Con_Printf(PRINT_DEVELOPER, "Bad tga file %s\n", name);
		return;
	}

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;

	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_index = LittleShort(*((short *) tmp));
	buf_p += 2;
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_length = LittleShort(*((short *) tmp));
	buf_p += 2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.y_origin = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.width = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.height = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	switch (targa_header.image_type) {
	case TGA_UNMAP_UNCOMP:
	case TGA_UNMAP_COMP:
		break;
	default:
		ri.Con_Printf(PRINT_ALL, "LoadTGA: Only type 2 and 10 targa RGB images supported (%s) (type: %i)\n", name, targa_header.image_type);
		ri.FS_FreeFile(buffer);
		return;
	}

	if (targa_header.colormap_type != 0 || (targa_header.pixel_size != 32 && targa_header.pixel_size != 24))
		ri.Sys_Error(ERR_DROP, "LoadTGA: Only 32 or 24 bit images supported (no colormaps) (%s) (pixel_size: %i)\n", name, targa_header.pixel_size);

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	targa_rgba = ri.TagMalloc(ri.imagePool, numPixels * 4, 0);
	*pic = targa_rgba;

	if (targa_header.id_length != 0)
		buf_p += targa_header.id_length;	/* skip TARGA image comment */

	switch (targa_header.image_type) {
	case TGA_UNMAP_UNCOMP:	/* Uncompressed, RGB images */
		for (row = rows - 1; row >= 0; row--) {
			pixbuf = targa_rgba + row * columns * 4;
			for (column = 0; column < columns; column++) {
				unsigned char red, green, blue, alphabyte;

				red = green = blue = alphabyte = 0;
				switch (targa_header.pixel_size) {
				case 24:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					alphabyte = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				default:
					break;
				}
			}
		}
		break;
	case TGA_UNMAP_COMP:	/* Runlength encoded RGB images */
		{
		unsigned char red, green, blue, alphabyte, packetHeader, packetSize, j;

		red = green = blue = alphabyte = 0;
		for (row = rows - 1; row >= 0; row--) {
			pixbuf = targa_rgba + row * columns * 4;
			for (column = 0; column < columns;) {
				packetHeader = *buf_p++;
				/* SCHAR_MAX although it's unsigned */
				packetSize = 1 + (packetHeader & SCHAR_MAX);
				if (packetHeader & 0x80) {	/* run-length packet */
					switch (targa_header.pixel_size) {
					case 24:
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alphabyte = 255;
						break;
					case 32:
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alphabyte = *buf_p++;
						break;
					default:
						break;
					}

					for (j = 0; j < packetSize; j++) {
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						column++;
						if (column == columns) {	/* run spans across rows */
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row * columns * 4;
						}
					}
				} else {		/* non run-length packet */
					for (j = 0; j < packetSize; j++) {
						switch (targa_header.pixel_size) {
						case 24:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
						case 32:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							alphabyte = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
						default:
							break;
						}
						column++;
						if (column == columns) {	/* pixel packet run spans across rows */
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row * columns * 4;
						}
					}
				}
			}
		  breakOut:;
		}
		}
	} /* switch */

	ri.FS_FreeFile(buffer);
}


/**
 * @brief
 * @sa LoadTGA
 */
extern void WriteTGA (FILE *f, byte *buffer, int width, int height)
{
	int		i, temp;
	byte	*out;
	size_t size;

	/* Allocate an output buffer */
	size = (width * height * 3) + 18;
	out = ri.TagMalloc(ri.imagePool, size, 0);

	/* Fill in header */
	out[2] = 2;		/* Uncompressed type */
	out[12] = width & 255;
	out[13] = width >> 8;
	out[14] = height & 255;
	out[15] = height >> 8;
	out[16] = 24;	/* Pixel size */

	/* Copy to temporary buffer */
	memcpy(out + 18, buffer, width * height * 3);

	/* Swap rgb to bgr */
	for (i = 18; i < size; i += 3) {
		temp = out[i];
		out[i] = out[i+2];
		out[i + 2] = temp;
	}

	if (fwrite (out, 1, size, f) != size)
		Com_Printf("Failed to write the tga file\n");

	ri.TagFree(out);
}


/*
=================================================================
JPEG LOADING
By Robert 'Heffo' Heffernan
=================================================================
*/

/**
 * @brief
 */
static void jpg_null (j_decompress_ptr cinfo)
{
}

/**
 * @brief
 */
static boolean jpg_fill_input_buffer (j_decompress_ptr cinfo)
{
	ri.Con_Printf(PRINT_ALL, "Premature end of JPEG data\n");
	return 1;
}

/**
 * @brief
 */
static void jpg_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	if (cinfo->src->bytes_in_buffer < (size_t) num_bytes)
		ri.Con_Printf(PRINT_ALL, "Premature end of JPEG data\n");

	cinfo->src->next_input_byte += (size_t) num_bytes;
	cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
}

/**
 * @brief
 */
static void jpeg_mem_src (j_decompress_ptr cinfo, byte * mem, int len)
{
	cinfo->src = (struct jpeg_source_mgr *) (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));
	cinfo->src->init_source = jpg_null;
	cinfo->src->fill_input_buffer = jpg_fill_input_buffer;
	cinfo->src->skip_input_data = jpg_skip_input_data;
	cinfo->src->resync_to_restart = jpeg_resync_to_restart;
	cinfo->src->term_source = jpg_null;
	cinfo->src->bytes_in_buffer = len;
	cinfo->src->next_input_byte = mem;
}

/**
 * @brief
 * @sa LoadPCX
 * @sa LoadTGA
 * @sa LoadPNG
 * @sa GL_FindImage
 */
static void LoadJPG (const char *filename, byte ** pic, int *width, int *height)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	byte *rawdata, *rgbadata, *scanline, *p, *q;
	int rawsize, i, components;

	if (*pic != NULL)
		Sys_Error("possible mem leak in LoadJPG\n");

	/* Load JPEG file into memory */
	rawsize = ri.FS_LoadFile(filename, (void **) &rawdata);

	if (!rawdata)
		return;

	/* Knightmare- check for bad data */
	if (rawdata[6] != 'J' || rawdata[7] != 'F' || rawdata[8] != 'I' || rawdata[9] != 'F') {
		ri.Con_Printf(PRINT_ALL, "Bad jpg file %s\n", filename);
		ri.FS_FreeFile(rawdata);
		return;
	}

	/* Initialise libJpeg Object */
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);

	/* Feed JPEG memory into the libJpeg Object */
	jpeg_mem_src(&cinfo, rawdata, rawsize);

	/* Process JPEG header */
	jpeg_read_header(&cinfo, qtrue);

	/* Start Decompression */
	jpeg_start_decompress(&cinfo);

	components = cinfo.output_components;
    if (components != 3 && components != 1) {
		Com_DPrintf("R_LoadJPG: Bad jpeg components '%s' (%d)\n", filename, components);
		jpeg_destroy_decompress(&cinfo);
		ri.FS_FreeFile(rawdata);
		return;
	}

	/* Check Colour Components */
	if (cinfo.output_components != 3 && cinfo.output_components != 4) {
		ri.Con_Printf(PRINT_ALL, "Invalid JPEG colour components\n");
		jpeg_destroy_decompress(&cinfo);
		ri.FS_FreeFile(rawdata);
		return;
	}

	/* Allocate Memory for decompressed image */
	rgbadata = ri.TagMalloc(ri.imagePool, cinfo.output_width * cinfo.output_height * 4, 0);
	if (!rgbadata) {
		ri.Con_Printf(PRINT_ALL, "Insufficient RAM for JPEG buffer\n");
		jpeg_destroy_decompress(&cinfo);
		ri.FS_FreeFile(rawdata);
		return;
	}
	/* Allocate Scanline buffer */
	scanline = ri.TagMalloc(ri.imagePool, cinfo.output_width * components, 0);
	if (!scanline) {
		ri.Con_Printf(PRINT_ALL, "Insufficient RAM for JPEG scanline buffer\n");
		ri.TagFree(rgbadata);
		jpeg_destroy_decompress(&cinfo);
		ri.FS_FreeFile(rawdata);
		return;
	}

	/* Pass sizes to output */
	*width = cinfo.output_width;
	*height = cinfo.output_height;

	/* Read Scanlines, and expand from RGB to RGBA */
	q = rgbadata;
	while (cinfo.output_scanline < cinfo.output_height) {
		p = scanline;
		jpeg_read_scanlines(&cinfo, &scanline, 1);

		if (components == 1) {
			for (i = 0; i < cinfo.output_width; i++, q += 4) {
				q[0] = q[1] = q[2] = *p++;
				q[3] = 255;
			}
		} else {
			for (i = 0; i < cinfo.output_width; i++, q += 4, p += 3) {
				q[0] = p[0], q[1] = p[1], q[2] = p[2];
				q[3] = 255;
			}
		}
	}

	/* Free the scanline buffer */
	ri.TagFree(scanline);

	/* Finish Decompression */
	jpeg_finish_decompress(&cinfo);

	/* Destroy JPEG object */
	jpeg_destroy_decompress(&cinfo);

	/* Return the 'rgbadata' */
	*pic = rgbadata;
	ri.FS_FreeFile(rawdata);
}

/* Expanded data destination object for stdio output */
typedef struct {
	struct jpeg_destination_mgr pub;	/* public fields */

	byte *outfile;				/* target stream */
	int size;
} my_destination_mgr;

typedef my_destination_mgr *my_dest_ptr;

/**
 * @brief Initialize destination --- called by jpeg_start_compress before any data is actually written
 */
static void init_destination (j_compress_ptr cinfo)
{
	my_dest_ptr dest = (my_dest_ptr) cinfo->dest;

	dest->pub.next_output_byte = dest->outfile;
	dest->pub.free_in_buffer = dest->size;
}

static size_t hackSize;

/**
 * @brief Terminate destination --- called by jpeg_finish_compress
 * after all data has been written.  Usually needs to flush buffer.
 *
 * @note *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */
static void term_destination (j_compress_ptr cinfo)
{
	my_dest_ptr dest = (my_dest_ptr) cinfo->dest;
	size_t datacount = dest->size - dest->pub.free_in_buffer;

	hackSize = datacount;
}

/**
 * @brief Empty the output buffer --- called whenever buffer fills up.
 *
 * In typical applications, this should write the entire output buffer
 * (ignoring the current state of next_output_byte & free_in_buffer),
 * reset the pointer & count to the start of the buffer, and return TRUE
 * indicating that the buffer has been dumped.
 *
 * In applications that need to be able to suspend compression due to output
 * overrun, a FALSE return indicates that the buffer cannot be emptied now.
 * In this situation, the compressor will return to its caller (possibly with
 * an indication that it has not accepted all the supplied scanlines).  The
 * application should resume compression after it has made more room in the
 * output buffer.  Note that there are substantial restrictions on the use of
 * suspension --- see the documentation.
 *
 * When suspending, the compressor will back up to a convenient restart point
 * (typically the start of the current MCU). next_output_byte & free_in_buffer
 * indicate where the restart point will be if the current call returns FALSE.
 * Data beyond this point will be regenerated after resumption, so do not
 * write it out when emptying the buffer externally.
 */
static boolean empty_output_buffer (j_compress_ptr cinfo)
{
	return TRUE;
}

/**
 * @brief Prepare for output to a stdio stream.
 * The caller must have already opened the stream, and is responsible
 * for closing it after finishing compression.
 */
static void jpegDest (j_compress_ptr cinfo, byte * outfile, int size)
{
	my_dest_ptr dest;

	/* The destination object is made permanent so that multiple JPEG images
	 * can be written to the same file without re-executing jpeg_stdio_dest.
	 * This makes it dangerous to use this manager and a different destination
	 * manager serially with the same JPEG object, because their private object
	 * sizes may be different.  Caveat programmer.
	 */
	if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
		cinfo->dest = (struct jpeg_destination_mgr *)
			(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(my_destination_mgr));
	}

	dest = (my_dest_ptr) cinfo->dest;
	dest->pub.init_destination = init_destination;
	dest->pub.empty_output_buffer = empty_output_buffer;
	dest->pub.term_destination = term_destination;
	dest->outfile = outfile;
	dest->size = size;
}

/**
 * @brief
 * @sa LoadJPG
 */
void SaveJPG (const char *filename, int quality, int image_width, int image_height, unsigned char *image_buffer)
{
	/* This struct contains the JPEG compression parameters and pointers to
	 * working space (which is allocated as needed by the JPEG library).
	 * It is possible to have several such structures, representing multiple
	 * compression/decompression processes, in existence at once.  We refer
	 * to any one struct (and its associated working data) as a "JPEG object".
	 */
	struct jpeg_compress_struct cinfo;

	/* This struct represents a JPEG error handler.  It is declared separately
	 * because applications often want to supply a specialized error handler
	 * (see the second half of this file for an example).  But here we just
	 * take the easy way out and use the standard error handler, which will
	 * print a message on stderr and call exit() if compression fails.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct jpeg_error_mgr jerr;

	/* More stuff */
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	int row_stride;				/* physical row width in image buffer */
	unsigned char *out;

	/* Step 1: allocate and initialize JPEG compression object */

	/* We have to set up the error handler first, in case the initialization
	 * step fails.  (Unlikely, but it could happen if you are out of memory.)
	 * This routine fills in the contents of struct jerr, and returns jerr's
	 * address which we place into the link field in cinfo.
	 */
	cinfo.err = jpeg_std_error(&jerr);
	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */
	/* Note: steps 2 and 3 can be done in either order. */

	/* Here we use the library-supplied code to send compressed data to a
	 * stdio stream.  You can also write your own code to do something else.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to write binary files.
	 */
	out = (unsigned char *)ri.TagMalloc(ri.imagePool, image_width * image_height * RGB_PIXELSIZE, 0);
	jpegDest(&cinfo, out, image_width * image_height * RGB_PIXELSIZE);

	/* Step 3: set parameters for compression */

	/* First we supply a description of the input image.
	 * Four fields of the cinfo struct must be filled in:
	 */
	cinfo.image_width = image_width;	/* image width and height, in pixels */
	cinfo.image_height = image_height;
	cinfo.input_components = RGB_PIXELSIZE;	/* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB;	/* colorspace of input image */
	/* Now use the library's routine to set default compression parameters.
	 * (You must set at least cinfo.in_color_space before calling this,
	 * since the defaults depend on the source color space.)
	 */
	jpeg_set_defaults(&cinfo);
	/* Now you can set any non-default parameters you wish to.
	 * Here we just illustrate the use of quality (quantization table) scaling:
	 */
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */ );

	/* Step 4: Start compressor */

	/* TRUE ensures that we will write a complete interchange-JPEG file.
	 * Pass TRUE unless you are very sure of what you're doing.
	 */
	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	 * loop counter, so that we don't have to keep track ourselves.
	 * To keep things simple, we pass one scanline per call; you can pass
	 * more if you wish, though.
	 */
	row_stride = image_width * RGB_PIXELSIZE;	/* JSAMPLEs per row in image_buffer */

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could pass
		 * more than one scanline at a time if that's more convenient.
		 */
		row_pointer[0] = &image_buffer[((cinfo.image_height - 1) * row_stride) - cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */
	jpeg_finish_compress(&cinfo);
	/* After finish_compress, we can close the output file. */
	ri.FS_WriteFile(filename, hackSize, (char *) out);

	ri.TagFree(out);

	/* Step 7: release JPEG compression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */
}

/**
 * @brief
 * @sa SaveJPG
 * @sa LoadJPG
 */
extern size_t SaveJPGToBuffer (byte * buffer, int quality, int image_width, int image_height, byte * image_buffer)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	/* pointer to JSAMPLE row[s] */
	JSAMPROW row_pointer[1];

	/* physical row width in image buffer */
	int row_stride;

	/* Step 1: allocate and initialize JPEG compression object */
	cinfo.err = jpeg_std_error(&jerr);
	/* colorspace */
	cinfo.jpeg_color_space = JCS_RGB;

	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */
	/* Note: steps 2 and 3 can be done in either order. */
	jpegDest(&cinfo, buffer, image_width * image_height * RGB_PIXELSIZE);

	/* Step 3: set parameters for compression */
	/* image width and height, in pixels */
	cinfo.image_width = image_width;
	cinfo.image_height = image_height;
	/* # of color components per pixel */
	cinfo.input_components = RGB_PIXELSIZE;
	/* colorspace of input image */
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	/* limit to baseline-JPEG values */
	jpeg_set_quality(&cinfo, quality, TRUE);
	/* If quality is set high, disable chroma subsampling */
	if (quality >= 85) {
		cinfo.comp_info[0].h_samp_factor = 1;
		cinfo.comp_info[0].v_samp_factor = 1;
	}

	/* Step 4: Start compressor */
	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */
	/* JSAMPLEs per row in image_buffer */
	row_stride = image_width * RGB_PIXELSIZE;

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could pass
		 * more than one scanline at a time if that's more convenient. */
		row_pointer[0] = &image_buffer[((cinfo.image_height - 1) * row_stride) - cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */
	jpeg_finish_compress(&cinfo);

	/* Step 7: release JPEG compression object */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */
	return hackSize;
}

/**
 * @brief
 * @sa GL_ScreenShot_f
 * @sa SaveJPG
 * @sa LoadJPG
 */
void WriteJPG (FILE *f, byte *buffer, int width, int height, int quality)
{
	int			offset, w3;
	struct		jpeg_compress_struct	cinfo;
	struct		jpeg_error_mgr			jerr;
	byte		*s;

	/* Initialise the jpeg compression object */
	cinfo.err = jpeg_std_error (&jerr);
	jpeg_create_compress (&cinfo);
	jpeg_stdio_dest (&cinfo, f);

	/* Setup jpeg parameters */
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.in_color_space = JCS_RGB;
	cinfo.input_components = 3;
	cinfo.progressive_mode = TRUE;

	jpeg_set_defaults (&cinfo);
	jpeg_set_quality (&cinfo, quality, TRUE);
	jpeg_start_compress (&cinfo, qtrue);	/* start compression */
	jpeg_write_marker (&cinfo, JPEG_COM, (byte *) "UFO:AI", (uint32_t) strlen ("UFO:AI"));

	/* Feed scanline data */
	w3 = cinfo.image_width * 3;
	offset = w3 * cinfo.image_height - w3;
	while (cinfo.next_scanline < cinfo.image_height) {
		s = &buffer[offset - (cinfo.next_scanline * w3)];
		jpeg_write_scanlines (&cinfo, &s, 1);
	}

	/* Finish compression */
	jpeg_finish_compress (&cinfo);
	jpeg_destroy_compress (&cinfo);
}

/**
 * @brief
 */
static void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
{
	int i, j;
	unsigned *inrow, *inrow2;
	unsigned frac, fracstep;
	unsigned p1[1024], p2[1024];
	byte *pix1, *pix2, *pix3, *pix4;

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;
	for (i = 0; i < outwidth; i++) {
		p1[i] = 4 * (frac >> 16);
		frac += fracstep;
	}
	frac = 3 * (fracstep >> 2);
	for (i = 0; i < outwidth; i++) {
		p2[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++, out += outwidth) {
		inrow = in + inwidth * (int) ((i + 0.25) * inheight / outheight);
		inrow2 = in + inwidth * (int) ((i + 0.75) * inheight / outheight);
		frac = fracstep >> 1;
		for (j = 0; j < outwidth; j++) {
			pix1 = (byte *) inrow + p1[j];
			pix2 = (byte *) inrow + p2[j];
			pix3 = (byte *) inrow2 + p1[j];
			pix4 = (byte *) inrow2 + p2[j];
			((byte *) (out + j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			((byte *) (out + j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			((byte *) (out + j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			((byte *) (out + j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
		}
	}

	if (r_anisotropic->integer && gl_state.anisotropic)
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, r_anisotropic->value);
	if (r_texture_lod->integer && gl_state.lod_bias)
		qglTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, r_texture_lod->value);
}

/**
 * @brief Scale up the pixel values in a texture to increase the lighting range
 */
static void GL_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma)
{
	if (gl_combine || only_gamma) {
		int i, c;
		byte *p;

		p = (byte *) in;

		c = inwidth * inheight;
		for (i = 0; i < c; i++, p += 4) {
			p[0] = gammatable[p[0]];
			p[1] = gammatable[p[1]];
			p[2] = gammatable[p[2]];
		}
	} else {
		int i, c;
		byte *p;

		p = (byte *) in;

		c = inwidth * inheight;
		for (i = 0; i < c; i++, p += 4) {
			p[0] = gammatable[intensitytable[p[0]]];
			p[1] = gammatable[intensitytable[p[1]]];
			p[2] = gammatable[intensitytable[p[2]]];
		}
	}
}

/**
 * @brief Operates in place, quartering the size of the texture
 */
static void GL_MipMap (byte * in, int width, int height)
{
	int i, j;
	byte *out;

	width <<= 2;
	height >>= 1;
	out = in;
	for (i = 0; i < height; i++, in += width)
		for (j = 0; j < width; j += 8, out += 4, in += 8) {
			out[0] = (in[0] + in[4] + in[width + 0] + in[width + 4]) >> 2;
			out[1] = (in[1] + in[5] + in[width + 1] + in[width + 5]) >> 2;
			out[2] = (in[2] + in[6] + in[width + 2] + in[width + 6]) >> 2;
			out[3] = (in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
		}
}

#define FILTER_SIZE	5
#define BLUR_FILTER	0
#define LIGHT_BLUR	1
#define EDGE_FILTER	2
#define EMBOSS_FILTER	3
#define EMBOSS_FILTER_LOW	4
#define EMBOSS_FILTER_HIGH	5
#define EMBOSS_FILTER_2	6
#define DARKEN_FILTER 7
#define SHARPEN_FILTER 8

/**
 * @brief
 */
static const float FilterMatrix[][FILTER_SIZE][FILTER_SIZE] = {
	/* regular blur */
	{
	 {0, 0, 0, 0, 0},
	 {0, 1, 1, 1, 0},
	 {0, 1, 1, 1, 0},
	 {0, 1, 1, 1, 0},
	 {0, 0, 0, 0, 0},
	 },
	/* light blur */
	{
	 {0, 0, 0, 0, 0},
	 {0, 1, 1, 1, 0},
	 {0, 1, 4, 1, 0},
	 {0, 1, 1, 1, 0},
	 {0, 0, 0, 0, 0},
	 },
	/* find edges */
	{
	 {0, 0, 0, 0, 0},
	 {0, -1, -1, -1, 0},
	 {0, -1, 8, -1, 0},
	 {0, -1, -1, -1, 0},
	 {0, 0, 0, 0, 0},
	 },
	/* emboss */
	{
	 {-1, -1, -1, -1, 0},
	 {-1, -1, -1, 0, 1},
	 {-1, -1, 0, 1, 1},
	 {-1, 0, 1, 1, 1},
	 {0, 1, 1, 1, 1},
	 },
	/* emboss_low */
	{
	 {-0.7, -0.7, -0.7, -0.7, 0},
	 {-0.7, -0.7, -0.7,  0, 0.7},
	 {-0.7, -0.7,  0,  0.7, 0.7},
	 {-0.7,  0,  0.7,  0.7, 0.7},
	 { 0,  0.7,  0.7,  0.7, 0.7},
	 },
	/* emboss_high */
	{
	 {-2, -2, -2, -2, 0},
	 {-2, -2, -2, 0, 2},
	 {-2, -1, 0, 2, 2},
	 {-2, 0, 2, 2, 2},
	 {0, 2, 2, 2, 2},
	 },
	/* emboss2 */
	{
	 {1, 1, 1, 1, 0},
	 {1, 1, 1, 0, -1},
	 {1, 1, 0, -1, -1},
	 {1, 0, -1, -1, -1},
	 {0, -1, -1, -1, -1},
	 },
	/* darken */
	{
	 {0, 0, 0, 0, 0},
	 {0, 0, 0, 0, 0},
	 {0, 0, 0, 0, 0},
	 {0, 0, 0, 0, 0},
	 {0, 0, 0, 0, 0},
	},
	/* sharpen */
	{
	 {1, 2,  0,  -2,   1},
	 {4, 8,  0,  -8,  -4},
	 {6, 12, 0, -12,  -6},
	 {4, 8,  0,  -8,  -4},
	 {1, 2,  0,  -2,  -1}
	}
};

/**
 * @brief Applies a 5 x 5 filtering matrix to the texture, then runs it through a simulated OpenGL texture environment
 * blend with the original data to derive a new texture.  Freaky, funky, and *f--king* *fantastic*.  You can do
 * reasonable enough "fake bumpmapping" with this baby...
 *
 * Filtering algorithm from http://www.student.kuleuven.ac.be/~m0216922/CG/filtering.html
 * All credit due
 */
static void R_FilterTexture (int filterindex, unsigned int *data, int width, int height, float factor, float bias, qboolean greyscale, int blend)
{
	int i;
	int x;
	int y;
	int filterX;
	int filterY;
	unsigned int *temp;
	size_t temp_size = width * height * 4;

	/* allocate a temp buffer */
	temp = ri.TagMalloc(ri.imagePool, temp_size, 0);
	if (!temp) {
		ri.Sys_Error(ERR_FATAL, "TagMalloc: failed on allocation of "UFO_SIZE_T" bytes", temp_size);
		return;					/* never reached. need for code analyst. */
	}

	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++) {
			float rgbFloat[3] = { 0, 0, 0 };

			for (filterX = 0; filterX < FILTER_SIZE; filterX++) {
				for (filterY = 0; filterY < FILTER_SIZE; filterY++) {
					int imageX = (x - (FILTER_SIZE / 2) + filterX + width) % width;
					int imageY = (y - (FILTER_SIZE / 2) + filterY + height) % height;

					/* casting's a unary operation anyway, so the othermost set of brackets in the left part */
					/* of the rvalue should not be necessary... but i'm paranoid when it comes to C... */
					rgbFloat[0] += ((float) ((byte *) & data[imageY * width + imageX])[0]) * FilterMatrix[filterindex][filterX][filterY];
					rgbFloat[1] += ((float) ((byte *) & data[imageY * width + imageX])[1]) * FilterMatrix[filterindex][filterX][filterY];
					rgbFloat[2] += ((float) ((byte *) & data[imageY * width + imageX])[2]) * FilterMatrix[filterindex][filterX][filterY];
				}
			}

			/* multiply by factor, add bias, and clamp */
			for (i = 0; i < 3; i++) {
				rgbFloat[i] *= factor;
				rgbFloat[i] += bias;

				if (rgbFloat[i] < 0)
					rgbFloat[i] = 0;
				if (rgbFloat[i] > 255)
					rgbFloat[i] = 255;
			}

			if (greyscale) {
				/* NTSC greyscale conversion standard */
				float avg = (rgbFloat[0] * 30 + rgbFloat[1] * 59 + rgbFloat[2] * 11) / 100;

				/* divide by 255 so GL operations work as expected */
				rgbFloat[0] = avg / 255.0;
				rgbFloat[1] = avg / 255.0;
				rgbFloat[2] = avg / 255.0;
			}

			/* write to temp - first, write data in (to get the alpha channel quickly and */
			/* easily, which will be left well alone by this particular operation...!) */
			temp[y * width + x] = data[y * width + x];

			/* now write in each element, applying the blend operator.  blend */
			/* operators are based on standard OpenGL TexEnv modes, and the */
			/* formulae are derived from the OpenGL specs (http://www.opengl.org). */
			for (i = 0; i < 3; i++) {
				/* divide by 255 so GL operations work as expected */
				float TempTarget;
				float SrcData = ((float) ((byte *) & data[y * width + x])[i]) / 255.0;

				switch (blend) {
				case BLEND_ADD:
					TempTarget = rgbFloat[i] + SrcData;
					break;

				case BLEND_BLEND:
					/* default is FUNC_ADD here */
					/* CsS + CdD works out as Src * Dst * 2 */
					TempTarget = rgbFloat[i] * SrcData * 2.0;
					break;

				case BLEND_REPLACE:
					TempTarget = rgbFloat[i];
					break;

				case BLEND_FILTER:
					/* same as default */
				default:
					TempTarget = rgbFloat[i] * SrcData;
					break;
				}

				/* multiply back by 255 to get the proper byte scale */
				TempTarget *= 255.0;

				/* bound the temp target again now, cos the operation may have thrown it out */
				if (TempTarget < 0)
					TempTarget = 0;
				if (TempTarget > 255)
					TempTarget = 255;

				/* and copy it in */
				((byte *) & temp[y * width + x])[i] = (byte) TempTarget;
			}
		}
	}

#if 0
	/* copy temp back to data */
	for (i = 0; i < (width * height); i++)
		data[i] = temp[i];
#else
	memcpy(data, temp, width * height * 4);
#endif
	/* release the temp buffer */
	ri.TagFree(temp);
}

static int upload_width, upload_height;
static unsigned scaled_buffer[1024 * 1024];

/**
 * @brief
 * @return has_alpha
 */
static qboolean GL_Upload32 (unsigned *data, int width, int height, qboolean mipmap, qboolean clamp, imagetype_t type, image_t* image)
{
	unsigned *scaled;
	int samples;
	int scaled_width, scaled_height;
	int i, c;
	int size;
	byte *scan;

	for (scaled_width = 1; scaled_width < width; scaled_width <<= 1);
	if (gl_round_down->integer && scaled_width > width && mipmap)
		scaled_width >>= 1;
	for (scaled_height = 1; scaled_height < height; scaled_height <<= 1);
	if (gl_round_down->integer && scaled_height > height && mipmap)
		scaled_height >>= 1;

	/* let people sample down the world textures for speed */
	if (mipmap) {
		scaled_width >>= gl_picmip->integer;
		scaled_height >>= gl_picmip->integer;
	}

	/* don't ever bother with > 2048*2048 textures */
	if (scaled_width > 2048)
		scaled_width = 2048;
	if (scaled_height > 2048)
		scaled_height = 2048;

	while (scaled_width > gl_maxtexres->value || scaled_height > gl_maxtexres->value) {
		scaled_width >>= 1;
		scaled_height >>= 1;
	}

	if (scaled_width < 1)
		scaled_width = 1;
	if (scaled_height < 1)
		scaled_height = 1;

	upload_width = scaled_width;
	upload_height = scaled_height;

	/* scan the texture for any non-255 alpha */
	c = width * height;
	scan = ((byte *) data) + 3;
	samples = gl_compressed_solid_format ? gl_compressed_solid_format : gl_solid_format;
	for (i = 0; i < c; i++, scan += 4) {
		if (*scan != 255) {
			samples = gl_compressed_alpha_format ? gl_compressed_alpha_format : gl_alpha_format;
			break;
		}
	}

	/* emboss filter */
	if (gl_imagefilter->integer && image && image->shader) {
		Com_DPrintf("Using image filter %s\n", image->shader->name);
		if (image->shader->emboss)
			R_FilterTexture(EMBOSS_FILTER, data, width, height, 1, 128, qtrue, image->shader->glMode);
		if (image->shader->emboss2)
			R_FilterTexture(EMBOSS_FILTER_2, data, width, height, 1, 128, qtrue, image->shader->glMode);
		if (image->shader->embossHigh)
			R_FilterTexture(EMBOSS_FILTER_HIGH, data, width, height, 1, 128, qtrue, image->shader->glMode);
		if (image->shader->embossLow)
			R_FilterTexture(EMBOSS_FILTER_LOW, data, width, height, 1, 128, qtrue, image->shader->glMode);
		if (image->shader->blur)
			R_FilterTexture(BLUR_FILTER, data, width, height, 1, 128, qtrue, image->shader->glMode);
		if (image->shader->light)
			R_FilterTexture(LIGHT_BLUR, data, width, height, 1, 128, qtrue, image->shader->glMode);
		if (image->shader->edge)
			R_FilterTexture(EDGE_FILTER, data, width, height, 1, 128, qtrue, image->shader->glMode);
	}

	if (scaled_width == width && scaled_height == height) {
		if (!mipmap) {
			qglTexImage2D(GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			goto done;
		}
		/* directly use the incoming data */
		scaled = data;
		size = 0;
	} else {
		/* allocate memory for scaled texture */
		scaled = scaled_buffer;
		while (scaled_width > 1024)
			scaled_width >>= 1;
		while (scaled_height > 1024)
			scaled_height >>= 1;

		GL_ResampleTexture(data, width, height, scaled, scaled_width, scaled_height);
	}

	GL_LightScaleTexture(scaled, scaled_width, scaled_height, !mipmap);

	qglTexImage2D(GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);

	if (mipmap) {
		int miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1) {
			GL_MipMap((byte *) scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
			qglTexImage2D(GL_TEXTURE_2D, miplevel, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
		}
	}
  done:;

	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (mipmap) ? gl_filter_min : gl_filter_max);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	if (clamp) {
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	}

	if (r_anisotropic->integer && gl_state.anisotropic)
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, r_anisotropic->value);
	if (r_texture_lod->integer && gl_state.lod_bias)
		qglTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, r_texture_lod->value);

	return (samples == gl_alpha_format || samples == gl_compressed_alpha_format);
}

/**
 * @brief
 * @return has_alpha
 */
static qboolean GL_Upload8 (byte * data, int width, int height, qboolean mipmap, imagetype_t type, image_t* image)
{
	unsigned *trans;
	size_t trans_size = 512 * 256 * sizeof(trans[0]);
	int s = width * height;
	int i, p;
	qboolean ret;

	trans = ri.TagMalloc(ri.imagePool, trans_size, 0);
	if (!trans) {
		ri.Sys_Error(ERR_FATAL, "TagMalloc: failed on allocation of "UFO_SIZE_T" bytes", trans_size);
		return qfalse;			/* never reached. need for code analyst. */
	}

	if (s > trans_size / 4)
		ri.Sys_Error(ERR_DROP, "GL_Upload8: too large");

	for (i = 0; i < s; i++) {
		p = data[i];
		trans[i] = d_8to24table[p];

		if (p == 255) {
			/* transparent, so scan around for another color */
			/* to avoid alpha fringes */
			/* FIXME: do a full flood fill so mips work... */
			if (i > width && data[i - width] != 255)
				p = data[i - width];
			else if (i < s - width && data[i + width] != 255)
				p = data[i + width];
			else if (i > 0 && data[i - 1] != 255)
				p = data[i - 1];
			else if (i < s - 1 && data[i + 1] != 255)
				p = data[i + 1];
			else
				p = 0;
			/* copy rgb components */
			((byte *) & trans[i])[0] = ((byte *) & d_8to24table[p])[0];
			((byte *) & trans[i])[1] = ((byte *) & d_8to24table[p])[1];
			((byte *) & trans[i])[2] = ((byte *) & d_8to24table[p])[2];
		}
	}

	ret = GL_Upload32(trans, width, height, mipmap, qtrue, type, image);
	ri.TagFree(trans);

	return ret;
}


#define DAN_WIDTH	512
#define DAN_HEIGHT	256

#define DAWN		0.03

static byte DaNalpha[DAN_WIDTH * DAN_HEIGHT];
image_t *DaN;

/**
 * @brief Applies alpha values to the night overlay image for 2d geoscape
 * @param[in] q
 */
void GL_CalcDayAndNight (float q)
{
	int x, y;
	float phi, dphi, a, da;
	float sin_q, cos_q, root;
	float pos;
	float sin_phi[DAN_WIDTH], cos_phi[DAN_WIDTH];
	byte *px;

	/* get image */
	if (!DaN) {
		if (numgltextures >= MAX_GLTEXTURES)
			ri.Sys_Error(ERR_DROP, "MAX_GLTEXTURES");
		DaN = &gltextures[numgltextures++];
		DaN->width = DAN_WIDTH;
		DaN->height = DAN_HEIGHT;
		DaN->type = it_pic;
		DaN->texnum = TEXNUM_IMAGES + (DaN - gltextures);
	}
	GL_Bind(DaN->texnum);

	/* init geometric data */
	dphi = (float) 2 * M_PI / DAN_WIDTH;

	da = M_PI / 2 * (HIGH_LAT - LOW_LAT) / DAN_HEIGHT;

	/* precalculate trigonometric functions */
	sin_q = sin(q);
	cos_q = cos(q);
	for (x = 0; x < DAN_WIDTH; x++) {
		phi = x * dphi - q;
		sin_phi[x] = sin(phi);
		cos_phi[x] = cos(phi);
	}

	/* calculate */
	px = DaNalpha;
	for (y = 0; y < DAN_HEIGHT; y++) {
		a = sin(M_PI / 2 * HIGH_LAT - y * da);
		root = sqrt(1 - a * a);
		for (x = 0; x < DAN_WIDTH; x++) {
			pos = sin_phi[x] * root * sin_q - (a * SIN_ALPHA + cos_phi[x] * root * COS_ALPHA) * cos_q;

			if (pos >= DAWN)
				*px++ = 255;
			else if (pos <= -DAWN)
				*px++ = 0;
			else
				*px++ = (byte) (128.0 * (pos / DAWN + 1));
		}
	}

	/* upload alpha map */
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, DAN_WIDTH, DAN_HEIGHT, 0, GL_ALPHA, GL_UNSIGNED_BYTE, DaNalpha);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
}


/**
 * @brief This is also used as an entry point for the generated r_notexture
 */
image_t *GL_LoadPic (const char *name, byte * pic, int width, int height, imagetype_t type, int bits)
{
	image_t *image;
	int i;
	size_t len;

	/* find a free image_t */
	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
		if (!image->texnum)
			break;

	if (i == numgltextures) {
		if (numgltextures == MAX_GLTEXTURES)
			ri.Sys_Error(ERR_DROP, "MAX_GLTEXTURES");
		numgltextures++;
	}
	image = &gltextures[i];

	image->type = type;

	len = strlen(name);
	if (len >= sizeof(image->name))
		ri.Sys_Error(ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);
	Q_strncpyz(image->name, name, MAX_QPATH);
	image->registration_sequence = registration_sequence;
	/* drop extension */
	if (len >= 4 && (*image).name[len - 4] == '.')
		(*image).name[len - 4] = '\0';

	image->width = width;
	image->height = height;

	if (image->type == it_pic && strstr(image->name, "_noclamp"))
		image->type = it_wrappic;

	image->shader = GL_GetShaderForImage(image->name);
	if (image->shader)
		Com_DPrintf("GL_LoadPic: Shader found: '%s'\n", image->name);

	/* load little pics into the scrap */
	if (image->type == it_pic && bits == 8 && image->width < 64 && image->height < 64) {
		int x = 0, y = 0;
		int i, j, k;
		int texnum;

		texnum = Scrap_AllocBlock(image->width, image->height, &x, &y);
		if (texnum == -1)
			goto nonscrap;
		scrap_dirty = qtrue;

		/* copy the texels into the scrap block */
		k = 0;
		for (i = 0; i < image->height; i++)
			for (j = 0; j < image->width; j++, k++)
				scrap_texels[texnum][(y + i) * BLOCK_WIDTH + x + j] = pic[k];
		image->texnum = TEXNUM_SCRAPS + texnum;
		image->has_alpha = qtrue;
	} else {
	  nonscrap:
		image->texnum = TEXNUM_IMAGES + (image - gltextures);
		if (pic) {
			GL_Bind(image->texnum);
			if (bits == 8)
				image->has_alpha = GL_Upload8(pic, width, height, (image->type != it_pic), image->type, image);
			else
				image->has_alpha = GL_Upload32((unsigned *) pic, width, height,
										(image->type != it_pic), image->type == it_pic, image->type, image);
			image->upload_width = upload_width;	/* after power of 2 and scales */
			image->upload_height = upload_height;
		}
	}
	return image;
}


/**
 * @brief Loads a ID wall image format
 * basically pcx with 3 midmap levels
 * @sa GL_LoadPic
 * @param[in] bpp Should be 8 for wal images, and 32 for m32
 */
static image_t *GL_LoadWal (const char *name, int bpp)
{
	miptex_t *mt;
	int width, height, ofs;
	image_t *image;

	ri.FS_LoadFile(name, (void **) &mt);
	if (!mt) {
		ri.Con_Printf(PRINT_ALL, "GL_LoadWal: can't load %s\n", name);
		return r_notexture;
	}

	width = LittleLong(mt->width);
	height = LittleLong(mt->height);
	ofs = LittleLong(mt->offsets[0]);

	image = GL_LoadPic(name, (byte *) mt + ofs, width, height, it_wall, bpp);

	ri.FS_FreeFile((void *) mt);

	return image;
}


/**
 * @brief Finds an image for a shader
 */
image_t *GL_FindImageForShader (const char *name)
{
	int i;
	image_t *image;

	/* look for it */
	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
		if (!Q_strncmp(name, image->name, MAX_VAR))
			return image;
	return NULL;
}

/**
 * @brief Finds or loads the given image
 * @sa Draw_FindPic
 * @param pname Image name
 * @note the image name has to be at least 5 chars long
 * @sa LoadTGA
 * @sa LoadJPG
 * @sa LoadPNG
 * @sa LoadPCX
 */
#ifdef DEBUG
image_t *GL_FindImageDebug (const char *pname, imagetype_t type, char *file, int line)
#else
image_t *GL_FindImage (const char *pname, imagetype_t type)
#endif
{
	char lname[MAX_QPATH];
	char *ename;
	char *etex;
	image_t *image;
	int i;
	size_t len;
	byte *pic = NULL, *palette;
	int width, height;

	if (!pname)
		ri.Sys_Error(ERR_DROP, "GL_FindImage: NULL name");
	len = strlen(pname);
	if (len < 5)
		return NULL;			/*  ri.Sys_Error (ERR_DROP, "GL_FindImage: bad name: %s", name); */

	/* drop extension */
	Q_strncpyz(lname, pname, MAX_QPATH);
	if (lname[len - 4] == '.')
		len -= 4;
	ename = &(lname[len]);
	*ename = 0;

	/* look for it */
	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
		if (!Q_strncmp(lname, image->name, MAX_QPATH)) {
			image->registration_sequence = registration_sequence;
			return image;
		}

	/* look for it in the error list */
	etex = glerrortex;
	while ((len = strlen(etex)) != 0) {
		if (!Q_strncmp(lname, etex, MAX_QPATH))
			/* it's in the error list */
			return r_notexture;

		etex += len + 1;
	}

	/* load the pic from disk */
	image = NULL;
	pic = NULL;
	palette = NULL;

	strcpy(ename, ".tga");
	if (ri.FS_CheckFile(lname) != -1) {
		LoadTGA(lname, &pic, &width, &height);
		if (pic) {
			image = GL_LoadPic(lname, pic, width, height, type, 32);
			goto end;
		}
	}

	strcpy(ename, ".png");
	if (ri.FS_CheckFile(lname) != -1) {
		LoadPNG(lname, &pic, &width, &height);
		if (pic) {
			image = GL_LoadPic(lname, pic, width, height, type, 32);
			goto end;
		}
	}

	strcpy(ename, ".jpg");
	if (ri.FS_CheckFile(lname) != -1) {
		LoadJPG(lname, &pic, &width, &height);
		if (pic) {
			image = GL_LoadPic(lname, pic, width, height, type, 32);
			goto end;
		}
	}

	strcpy(ename, ".pcx");
	if (ri.FS_CheckFile(lname) != -1) {
		LoadPCX(lname, &pic, &palette, &width, &height);
		if (pic) {
			image = GL_LoadPic(lname, pic, width, height, type, 8);
			goto end;
		}
	}

	strcpy(ename, ".m32");
	if (ri.FS_CheckFile(lname) != -1) {
		image = GL_LoadWal(lname, 32);
		goto end;
	}

	strcpy(ename, ".wal");
	if (ri.FS_CheckFile(lname) != -1) {
		image = GL_LoadWal(lname, 8);
		goto end;
	}

	/* no fitting texture found */
	/* add to error list */
	image = r_notexture;

	*ename = 0;
#ifdef DEBUG
	ri.Con_Printf(PRINT_ALL, "GL_FindImage: Can't find %s (%s) - file: %s, line %i\n", lname, pname, file, line);
#else
	ri.Con_Printf(PRINT_ALL, "GL_FindImage: Can't find %s (%s)\n", lname, pname);
#endif

	if ((glerrortexend - glerrortex) + strlen(lname) < MAX_GLERRORTEX) {
		Q_strncpyz(glerrortexend, lname, MAX_QPATH);
		glerrortexend += strlen(lname) + 1;
	} else {
		ri.Sys_Error(ERR_DROP, "MAX_GLERRORTEX");
	}

  end:
	if (pic)
		ri.TagFree(pic);
	if (palette)
		ri.TagFree(palette);

	return image;
}



/**
 * @brief
 */
struct image_s *R_RegisterSkin (const char *name)
{
	return GL_FindImage(name, it_skin);
}

/**
 * @brief Any image that was not touched on this registration sequence will be freed
 */
void GL_FreeUnusedImages (void)
{
	int i;
	image_t *image;

	/* never free r_notexture or particle texture */
	r_notexture->registration_sequence = registration_sequence;
	r_particletexture->registration_sequence = registration_sequence;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++) {
		if (image->registration_sequence == registration_sequence)
			continue;			/* used this sequence */
		if (!image->registration_sequence)
			continue;			/* free image_t slot */
		if (image->type == it_pic || image->type == it_wrappic)
			continue;			/* fix this! don't free pics */
		/* free it */
		qglDeleteTextures(1, (GLuint *) &image->texnum);
		memset(image, 0, sizeof(*image));
	}
}


/**
 * @brief
 */
int Draw_GetPalette (void)
{
	int i;
	int r, g, b;
	unsigned v;
	byte *pic = NULL, *pal;
	int width, height;

	/* get the palette */
	LoadPCX("pics/colormap.pcx", &pic, &pal, &width, &height);
	if (!pal) {
		ri.Sys_Error(ERR_FATAL, "Couldn't load pics/colormap.pcx");
		return 0;				/* never reached. need for code analyst. */
	}

	for (i = 0; i < 256; i++) {
		r = pal[i * 3 + 0];
		g = pal[i * 3 + 1];
		b = pal[i * 3 + 2];

		v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
		d_8to24table[i] = LittleLong(v);
	}

	d_8to24table[255] &= LittleLong(0xffffff);	/* 255 is transparent */

	ri.TagFree(pic);
	ri.TagFree(pal);

	return 0;
}


/**
 * @brief
 */
void GL_InitImages (void)
{
	int i, j;
	float g = vid_gamma->value;

	registration_sequence = 1;
	numgltextures = 0;
	glerrortex[0] = 0;
	glerrortexend = glerrortex;
	DaN = NULL;

	/* init intensity conversions */
	gl_intensity = ri.Cvar_Get("gl_intensity", "2", CVAR_ARCHIVE, NULL);

	if (gl_intensity->value < 1)
		ri.Cvar_Set("gl_intensity", "1");

	gl_state.inverse_intensity = 1 / gl_intensity->value;

	Draw_GetPalette();

	if (gl_config.renderer & (GL_RENDERER_VOODOO | GL_RENDERER_VOODOO2))
		g = 1.0F;

	for (i = 0; i < 256; i++) {
		if (g == 1 || gl_state.hwgamma) {
			gammatable[i] = i;
		} else {
			float inf;

			inf = 255 * pow((i + 0.5) / 255.5, g) + 0.5;
			if (inf < 0)
				inf = 0;
			if (inf > 255)
				inf = 255;
			gammatable[i] = inf;
		}
	}

	for (i = 0; i < 256; i++) {
		j = i * gl_intensity->value;

		if (j > 255)
			j = 255;

		intensitytable[i] = j;
	}
}

/**
 * @brief
 */
void GL_ShutdownImages (void)
{
	int i;
	image_t *image;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++) {
		if (!image->registration_sequence)
			continue;			/* free image_t slot */
		/* free it */
		qglDeleteTextures(1, (GLuint *) & image->texnum);
		memset(image, 0, sizeof(*image));
	}
}
