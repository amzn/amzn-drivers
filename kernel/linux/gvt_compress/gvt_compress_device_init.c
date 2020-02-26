// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved. */

#include <linux/io.h>

#include "gvt_compress_device.h"
#include "gvt_compress_regs.h"

/**
 * Compression Algorithm Index
 */
enum gvt_comp_alg_idx {
	GVT_COMP_ALG_DEFLATE = 1,
};

/**
 * Compression Algorithm Type
 */
enum gvt_comp_algorithm_type {
	GVT_COMP_ALG_TYPE_STATIC_DEFLATE = 2,
};

/**
 * Compression form of compressed code word
 */
enum gvt_comp_format_idx {
	GVT_COMP_FMT_PROB_TABLES = 0,
};

/**
 * Compression output entry description ID
 */
enum gvt_comp_output_id {
	GVT_COMP_OUT_100_NORMAL = 0,
};

/**
 * Compression output modes
 *  - Normal: If compressed >= threshold then output original packet
 */
enum gvt_comp_output_mode {
	GVT_COMP_OUT_MODE_NORMAL = 0,
};

struct gvt_comp_huffman_static_tbl_entry {
	/* The Huffman coding for entry with appropriate distance. */
	unsigned int code;
	/* Huffman code length.
	 * The code should be lsb aligned for partial length.
	 */
	unsigned int length;
	/*
	 * Extra bits following the code found in the stream,
	 * used to determine the exact value.
	 * Irrelevant for literal and end-of-buffer entries where
	 * extra_bits==0.
	 */
	unsigned int extra_bits;
	/**
	 * The distance range for the encoded entry.
	 * Irrelevant for literal and end-of-buffer entries where
	 * dist==entry_id.
	 * For length entries only 9 lsb bits are used.
	 */
	unsigned int dist;
};

/** Compression encoder profile table entry */
struct gvt_comp_enc_prof_ent {
	enum gvt_comp_prof_id	id;
	unsigned int		history_size;
	enum gvt_comp_alg_idx	algorithm_index;
	uint8_t			compression_optimizer_en;
	uint8_t			force_original_sequence_out;
	uint8_t			force_encoded_sequence_out;
	unsigned int		output_controller_index;
	unsigned int		concat_next_symbol;
	unsigned int		max_match_threshold;
	unsigned int		min_match_threshold;
	unsigned int		no_match_length;
	unsigned int		uncompressed_length;
};

/** Compression decoder profile table entry */
struct gvt_comp_dec_prof_ent {
	enum gvt_comp_prof_id	id;
	unsigned int		history_size;
	unsigned int		match_length_base;
	uint8_t			force_original_sequence_out;
	uint8_t			compressed_flag_polarity;
	uint8_t			compressed_flag_en;
	unsigned int		compressed_flag_coalescing;
	uint8_t			field_offset_size;
	uint8_t			field_length_size;
	unsigned int		field_composition;
	unsigned int		algorithm_type;
	unsigned int		concat_next_symbol;
	uint8_t			offset_base;
	uint8_t			offset_endianity;
	uint8_t			huffman_extra_bits_swap;
	uint8_t			bit_swap;
};

/** Compression algorithm table entry */
struct gvt_comp_alg_ent {
	enum gvt_comp_alg_idx	id;
	unsigned int		flags_offset;
	unsigned int		flags_pos_0;
	unsigned int		flags_pos_1;
	unsigned int		flags_pos_2;
	unsigned int		flags_pos_3;
	uint8_t			flags_cfg_polarity;
	uint8_t			flags_cfg_en;
	unsigned int		algorithm_type;
	uint8_t			offset_base;
	uint8_t			offset_endianity;
	uint8_t			huffman_extra_bits_swap;
	uint8_t			bit_swap;
};

/** Compression format table entry */
struct gvt_comp_fmt_ent {
	enum gvt_comp_format_idx	id;
	unsigned int			code_word_offset;
	unsigned int			alu_0_input_symbol;
	unsigned int			alu_1_input_symbol;
	unsigned int			alu_0_input_length;
	unsigned int			alu_1_input_length;
	unsigned int			alu_0_input_offset;
	unsigned int			alu_1_input_offset;
	unsigned int			alu_0_command_symbol;
	unsigned int			alu_1_command_symbol;
	unsigned int			alu_0_command_length;
	unsigned int			alu_1_command_length;
	unsigned int			alu_0_command_offset;
	unsigned int			alu_1_command_offset;
	unsigned int			field_byte_count;
};

/**
 * Compression encoder output table entry
 */
struct gvt_comp_enc_out_ent {
	enum gvt_comp_output_id		id;
	enum gvt_comp_output_mode	mode;
	unsigned int			alu_const_0;
	unsigned int			alu_const_1;
	unsigned int			alu_const_2;
	unsigned int			alu_const_3;
	unsigned int			alu_const_4;
	unsigned int			alu_const_5;
	unsigned int			alu_in_stg_1_alu_0_mux_0;
	unsigned int			alu_in_stg_1_alu_0_mux_1;
	unsigned int			alu_in_stg_1_alu_0_mux_2;
	unsigned int			alu_in_stg_1_alu_0_mux_3;
	unsigned int			alu_in_stg_1_alu_1_mux_0;
	unsigned int			alu_in_stg_1_alu_1_mux_1;
	unsigned int			alu_in_stg_1_alu_1_mux_2;
	unsigned int			alu_in_stg_1_alu_1_mux_3;
	unsigned int			alu_in_stg_2_alu_mux_0;
	unsigned int			alu_in_stg_2_alu_mux_1;
	unsigned int			alu_in_stg_2_alu_mux_2;
	unsigned int			alu_in_stg_2_alu_mux_3;
	unsigned int			alu_opcode_stg_1_alu_0_op_0;
	unsigned int			alu_opcode_stg_1_alu_0_op_1;
	unsigned int			alu_opcode_stg_1_alu_0_op_2;
	unsigned int			alu_opcode_stg_1_alu_1_op_0;
	unsigned int			alu_opcode_stg_1_alu_1_op_1;
	unsigned int			alu_opcode_stg_1_alu_1_op_2;
	unsigned int			alu_opcode_stg_2_alu_op_0;
	unsigned int			alu_opcode_stg_2_alu_op_1;
	unsigned int			alu_opcode_stg_2_alu_op_2;
	uint8_t				alu_negative_res_mux_sel;
	uint8_t				alu_positive_res_mux_sel;
	uint8_t				alu_zero_res_mux_sel;
};

/** Compression encoder profile table entries */
static const struct gvt_comp_enc_prof_ent enc_prof_entries[] = {
		{
			/*Deflate 1KB History Stream 100% Normal*/
			.id = GVT_COMP_DEFLATE_1KB_HISTORY_100_NORMAL,
			.history_size = 4,
			.algorithm_index = GVT_COMP_ALG_DEFLATE,
			.compression_optimizer_en = 1,
			.force_original_sequence_out = 0,
			.force_encoded_sequence_out = 0,
			.output_controller_index = GVT_COMP_OUT_100_NORMAL,
			.concat_next_symbol = 0,
			.max_match_threshold = 258,
			.min_match_threshold = 3,
			.no_match_length = 0,
			.uncompressed_length = 0,
		},
		{
			/*Deflate 4KB History Stream 100% Normal*/
			.id = GVT_COMP_DEFLATE_4KB_HISTORY_100_NORMAL,
			.history_size = 16,
			.algorithm_index = GVT_COMP_ALG_DEFLATE,
			.compression_optimizer_en = 1,
			.force_original_sequence_out = 0,
			.force_encoded_sequence_out = 0,
			.output_controller_index = GVT_COMP_OUT_100_NORMAL,
			.concat_next_symbol = 0,
			.max_match_threshold = 258,
			.min_match_threshold = 3,
			.no_match_length = 0,
			.uncompressed_length = 0,
		},
		{
			/*Deflate 8KB History Stream 100% Normal*/
			.id = GVT_COMP_DEFLATE_8KB_HISTORY_100_NORMAL,
			.history_size = 32,
			.algorithm_index = GVT_COMP_ALG_DEFLATE,
			.compression_optimizer_en = 1,
			.force_original_sequence_out = 0,
			.force_encoded_sequence_out = 0,
			.output_controller_index = GVT_COMP_OUT_100_NORMAL,
			.concat_next_symbol = 0,
			.max_match_threshold = 258,
			.min_match_threshold = 3,
			.no_match_length = 0,
			.uncompressed_length = 0,
		},
		{
			/*Deflate 16KB History Stream 100% Normal*/
			.id = GVT_COMP_DEFLATE_16KB_HISTORY_100_NORMAL,
			.history_size = 64,
			.algorithm_index = GVT_COMP_ALG_DEFLATE,
			.compression_optimizer_en = 1,
			.force_original_sequence_out = 0,
			.force_encoded_sequence_out = 0,
			.output_controller_index = GVT_COMP_OUT_100_NORMAL,
			.concat_next_symbol = 0,
			.max_match_threshold = 258,
			.min_match_threshold = 3,
			.no_match_length = 0,
			.uncompressed_length = 0,
		},
		{
			/*Deflate 32KB History Stream 100% Normal*/
			.id = GVT_COMP_DEFLATE_32KB_HISTORY_100_NORMAL,
			.history_size = 128,
			.algorithm_index = GVT_COMP_ALG_DEFLATE,
			.compression_optimizer_en = 1,
			.force_original_sequence_out = 0,
			.force_encoded_sequence_out = 0,
			.output_controller_index = GVT_COMP_OUT_100_NORMAL,
			.concat_next_symbol = 0,
			.max_match_threshold = 258,
			.min_match_threshold = 3,
			.no_match_length = 0,
			.uncompressed_length = 0,
		},
	};

/** Compression decoder profile table entries */
static const struct gvt_comp_dec_prof_ent dec_prof_entries[] = {
		{
			/*Static Deflate 1KB History Stream*/
			.id = GVT_COMP_DEFLATE_1KB_HISTORY_100_NORMAL,
			.history_size = 4,
			.match_length_base = 3,
			.force_original_sequence_out = 0,
			.compressed_flag_polarity = 0,
			.compressed_flag_en = 0,
			.compressed_flag_coalescing = 0,
			.field_offset_size = 0,
			.field_length_size = 0,
			.field_composition = 0,
			.algorithm_type = GVT_COMP_ALG_TYPE_STATIC_DEFLATE,
			.concat_next_symbol = 0,
			.offset_base = 1,
			.offset_endianity = 0,
			.huffman_extra_bits_swap = 1,
			.bit_swap = 1,
		},
		{
			/*Static Deflate 4KB History Stream*/
			.id = GVT_COMP_DEFLATE_4KB_HISTORY_100_NORMAL,
			.history_size = 16,
			.match_length_base = 3,
			.force_original_sequence_out = 0,
			.compressed_flag_polarity = 0,
			.compressed_flag_en = 0,
			.compressed_flag_coalescing = 0,
			.field_offset_size = 0,
			.field_length_size = 0,
			.field_composition = 0,
			.algorithm_type = GVT_COMP_ALG_TYPE_STATIC_DEFLATE,
			.concat_next_symbol = 0,
			.offset_base = 1,
			.offset_endianity = 0,
			.huffman_extra_bits_swap = 1,
			.bit_swap = 1,
		},
		{
			/*Static Deflate 8KB History Stream*/
			.id = GVT_COMP_DEFLATE_8KB_HISTORY_100_NORMAL,
			.history_size = 32,
			.match_length_base = 3,
			.force_original_sequence_out = 0,
			.compressed_flag_polarity = 0,
			.compressed_flag_en = 0,
			.compressed_flag_coalescing = 0,
			.field_offset_size = 0,
			.field_length_size = 0,
			.field_composition = 0,
			.algorithm_type = GVT_COMP_ALG_TYPE_STATIC_DEFLATE,
			.concat_next_symbol = 0,
			.offset_base = 1,
			.offset_endianity = 0,
			.huffman_extra_bits_swap = 1,
			.bit_swap = 1,
		},
		{
			/*Static Deflate 16KB History Stream*/
			.id = GVT_COMP_DEFLATE_16KB_HISTORY_100_NORMAL,
			.history_size = 64,
			.match_length_base = 3,
			.force_original_sequence_out = 0,
			.compressed_flag_polarity = 0,
			.compressed_flag_en = 0,
			.compressed_flag_coalescing = 0,
			.field_offset_size = 0,
			.field_length_size = 0,
			.field_composition = 0,
			.algorithm_type = GVT_COMP_ALG_TYPE_STATIC_DEFLATE,
			.concat_next_symbol = 0,
			.offset_base = 1,
			.offset_endianity = 0,
			.huffman_extra_bits_swap = 1,
			.bit_swap = 1,
		},
		{
			/*Static Deflate 32KB History Stream*/
			.id = GVT_COMP_DEFLATE_32KB_HISTORY_100_NORMAL,
			.history_size = 128,
			.match_length_base = 3,
			.force_original_sequence_out = 0,
			.compressed_flag_polarity = 0,
			.compressed_flag_en = 0,
			.compressed_flag_coalescing = 0,
			.field_offset_size = 0,
			.field_length_size = 0,
			.field_composition = 0,
			.algorithm_type = GVT_COMP_ALG_TYPE_STATIC_DEFLATE,
			.concat_next_symbol = 0,
			.offset_base = 1,
			.offset_endianity = 0,
			.huffman_extra_bits_swap = 1,
			.bit_swap = 1,
		},
	};

/** Compression algorithm table entries */
static const struct gvt_comp_alg_ent alg_entries[] = {
		{
			/*Static Deflate Algorithm*/
			.id = GVT_COMP_ALG_DEFLATE,
			.flags_offset = 0,
			.flags_pos_0 = 0,
			.flags_pos_1 = 0,
			.flags_pos_2 = 0,
			.flags_pos_3 = 0,
			.flags_cfg_polarity = 0,
			.flags_cfg_en = 0,
			.algorithm_type = GVT_COMP_ALG_TYPE_STATIC_DEFLATE,
			.offset_base = 1,
			.offset_endianity = 0,
			.huffman_extra_bits_swap = 1,
			.bit_swap = 1,
		},
	};

/** Compression format table entries */
static const struct gvt_comp_fmt_ent fmt_entries[] = {
		{
			/*Static Deflate*/
			.id = GVT_COMP_FMT_PROB_TABLES,
			.code_word_offset = 0,
			.alu_0_input_symbol = 0,
			.alu_1_input_symbol = 0,
			.alu_0_input_length = 0,
			.alu_1_input_length = 0,
			.alu_0_input_offset = 0,
			.alu_1_input_offset = 0,
			.alu_0_command_symbol = 0,
			.alu_1_command_symbol = 0,
			.alu_0_command_length = 0,
			.alu_1_command_length = 0,
			.alu_0_command_offset = 0,
			.alu_1_command_offset = 0,
			.field_byte_count = 0,
		},
	};

/** Compression encoder output table entries */
static const struct gvt_comp_enc_out_ent enc_out_entries[] = {
		{
			.id = GVT_COMP_OUT_100_NORMAL,
			.mode = GVT_COMP_OUT_MODE_NORMAL,
			.alu_const_0 = 0,
			.alu_const_1 = 0,
			.alu_const_2 = 0,
			.alu_const_3 = 0,
			.alu_const_4 = 0,
			.alu_const_5 = 0,
			.alu_in_stg_1_alu_0_mux_0 = 7,
			.alu_in_stg_1_alu_0_mux_1 = 6,
			.alu_in_stg_1_alu_0_mux_2 = 0,
			.alu_in_stg_1_alu_0_mux_3 = 0,
			.alu_in_stg_1_alu_1_mux_0 = 0,
			.alu_in_stg_1_alu_1_mux_1 = 0,
			.alu_in_stg_1_alu_1_mux_2 = 0,
			.alu_in_stg_1_alu_1_mux_3 = 0,
			.alu_in_stg_2_alu_mux_0 = 8,
			.alu_in_stg_2_alu_mux_1 = 0,
			.alu_in_stg_2_alu_mux_2 = 0,
			.alu_in_stg_2_alu_mux_3 = 0,
			.alu_opcode_stg_1_alu_0_op_0 = 2,
			.alu_opcode_stg_1_alu_0_op_1 = 1,
			.alu_opcode_stg_1_alu_0_op_2 = 1,
			.alu_opcode_stg_1_alu_1_op_0 = 1,
			.alu_opcode_stg_1_alu_1_op_1 = 1,
			.alu_opcode_stg_1_alu_1_op_2 = 1,
			.alu_opcode_stg_2_alu_op_0 = 1,
			.alu_opcode_stg_2_alu_op_1 = 1,
			.alu_opcode_stg_2_alu_op_2 = 1,
			.alu_negative_res_mux_sel = true,
			.alu_positive_res_mux_sel = false,
			.alu_zero_res_mux_sel = false,
		},
	};


static void gvt_comp_alg_ents_init(
			void __iomem *comp_regs,
			const struct gvt_comp_alg_ent *entries,
			unsigned int num_entries)
{
	void __iomem *regs = comp_regs + GVT_COMP_ALG_OFF;

	for (; num_entries; num_entries--, entries++) {
		writel(entries->id, regs);
		writel(entries->flags_offset, regs + 0x4);
		writel(entries->flags_pos_0, regs + 0x8);
		writel(entries->flags_pos_1, regs + 0xc);
		writel(entries->flags_pos_2, regs + 0x10);
		writel(entries->flags_pos_3, regs + 0x14);
		writel((entries->flags_cfg_polarity ? 1 : 0) |
		       (entries->flags_cfg_en ? 2 : 0),
		       regs + 0x18);
		writel((entries->algorithm_type) |
			(entries->offset_base ? (1 << 4) : 0) |
			(entries->offset_endianity ? (1 << 5) : 0) |
			(entries->huffman_extra_bits_swap ? (1 << 6) : 0) |
			(entries->bit_swap ? (1 << 7) : 0),
			regs + 0x1c);
	}
}

static void gvt_comp_enc_prof_init(
			void __iomem *comp_regs,
			const struct gvt_comp_enc_prof_ent *entries,
			unsigned int num_entries)
{
	void __iomem *regs = comp_regs + GVT_COMP_ENC_PROF_OFF;

	for (; num_entries; num_entries--, entries++) {
		writel(entries->id, regs);
		writel((entries->history_size << 0) |
			(entries->algorithm_index << 8) |
			(entries->compression_optimizer_en ? (1 << 12) : 0) |
			(entries->force_original_sequence_out ? (1 << 13) : 0) |
			(entries->force_encoded_sequence_out ? (1 << 14) : 0) |
			((entries->output_controller_index << 15) & 0x18000) |
			(entries->concat_next_symbol ? (1 << 17) : 0) |
			((entries->output_controller_index >> 2) << 18),
			regs + 0x4);
		writel((entries->max_match_threshold << 0) |
			(entries->min_match_threshold << 16),
			regs + 0x8);
		writel((entries->no_match_length << 0) |
			(entries->uncompressed_length << 8),
			regs + 0xc);
	}
}

static void gvt_comp_dec_prof_init(
			void __iomem *comp_regs,
			const struct gvt_comp_dec_prof_ent *entries,
			unsigned int num_entries)
{
	void __iomem *regs = comp_regs + GVT_COMP_DEC_PROF_OFF;

	for (; num_entries; num_entries--, entries++) {
		writel(entries->id, regs);
		writel((entries->history_size) |
			(entries->match_length_base << 8) |
			(entries->force_original_sequence_out ? (1 << 16) : 0),
			regs + 0x4);
		writel((entries->compressed_flag_polarity ? (1 << 0) : 0) |
			(entries->compressed_flag_en ? (1 << 1) : 0) |
			(entries->compressed_flag_coalescing << 2) |
			(entries->field_offset_size ? (1 << 4) : 0) |
			(entries->field_length_size ? (1 << 5) : 0) |
			(entries->field_composition << 6) |
			(entries->algorithm_type << 9) |
			(entries->concat_next_symbol ? (1 << 11) : 0) |
			(entries->offset_base ? (1 << 12) : 0) |
			(entries->offset_endianity ? (1 << 13) : 0) |
			(entries->huffman_extra_bits_swap ? (1 << 14) : 0) |
			(entries->bit_swap ? (1 << 15) : 0),
			regs + 0x8);
	}
}

static void gvt_comp_fmt_init(
			void __iomem *comp_regs,
			const struct gvt_comp_fmt_ent *entries,
			unsigned int num_entries)
{
	void __iomem *regs = comp_regs + GVT_COMP_FMT_OFF;

	for (; num_entries; num_entries--, entries++) {
		writel(entries->id, regs);
		writel(entries->code_word_offset, regs + 0x4);
		writel(entries->alu_0_input_symbol, regs + 0x8);
		writel(entries->alu_1_input_symbol, regs + 0xc);
		writel(entries->alu_0_input_length, regs + 0x10);
		writel(entries->alu_1_input_length, regs + 0x14);
		writel(entries->alu_0_input_offset, regs + 0x18);
		writel(entries->alu_1_input_offset, regs + 0x1c);
		writel((entries->alu_0_command_symbol) |
			(entries->alu_1_command_symbol << 4) |
			(entries->alu_0_command_length << 8) |
			(entries->alu_1_command_length << 12) |
			(entries->alu_0_command_offset << 16) |
			(entries->alu_1_command_offset << 20) |
			(entries->field_byte_count << 24),
			regs + 0x20);
	}
}

static void gvt_comp_enc_out_init(
			void __iomem *comp_regs,
			const struct gvt_comp_enc_out_ent *entries,
			unsigned int num_entries)
{
	void __iomem *regs = comp_regs + GVT_COMP_ENC_OUT_OFF;

	for (; num_entries; num_entries--, entries++) {
		writel(entries->id, regs);
		writel(entries->alu_const_0 | (entries->alu_const_1 << 16),
			regs + 0x4);
		writel(entries->alu_const_2 | (entries->alu_const_3 << 16),
			regs + 0x8);
		writel(entries->alu_const_4 | (entries->alu_const_5 << 16),
			regs + 0xc);
		writel((entries->alu_in_stg_1_alu_0_mux_0 << 0) |
			(entries->alu_in_stg_1_alu_0_mux_1 << 4) |
			(entries->alu_in_stg_1_alu_0_mux_2 << 8) |
			(entries->alu_in_stg_1_alu_0_mux_3 << 12) |
			(entries->alu_in_stg_1_alu_1_mux_0 << 16) |
			(entries->alu_in_stg_1_alu_1_mux_1 << 20) |
			(entries->alu_in_stg_1_alu_1_mux_2 << 24) |
			(entries->alu_in_stg_1_alu_1_mux_3 << 28),
			regs + 0x10);
		writel((entries->alu_in_stg_2_alu_mux_0 << 0) |
			(entries->alu_in_stg_2_alu_mux_1 << 4) |
			(entries->alu_in_stg_2_alu_mux_2 << 8) |
			(entries->alu_in_stg_2_alu_mux_3 << 12),
			regs + 0x14);
		writel((entries->alu_opcode_stg_1_alu_0_op_0 << 0) |
			(entries->alu_opcode_stg_1_alu_0_op_1 << 4) |
			(entries->alu_opcode_stg_1_alu_0_op_2 << 8) |
			(entries->alu_opcode_stg_1_alu_1_op_0 << 12) |
			(entries->alu_opcode_stg_1_alu_1_op_1 << 16) |
			(entries->alu_opcode_stg_1_alu_1_op_2 << 20),
			regs + 0x18);
		writel((entries->alu_opcode_stg_2_alu_op_0 << 0) |
			(entries->alu_opcode_stg_2_alu_op_1 << 4) |
			(entries->alu_opcode_stg_2_alu_op_2 << 8),
			regs + 0x1c);
		writel((entries->alu_negative_res_mux_sel ? 1 : 0) |
			(entries->alu_positive_res_mux_sel ? 2 : 0) |
			(entries->alu_zero_res_mux_sel ? 4 : 0) |
			(entries->mode << 3),
			regs + 0x20);
	}
}

static void gvt_comp_huffman_static_tbl_init(void __iomem *comp_regs)
{
	struct gvt_comp_huffman_static_tbl_entry entry;
	void __iomem *tbl_reg;
	unsigned int idx;

	tbl_reg = comp_regs + GVT_COMP_HUFFMAN_TBL_OFF;

	for (idx = 0; idx < GVT_COMP_HUFFMAN_STATIC_TABLE_NUM_ENTRIES; idx++) {
		if (idx <= 143) {
			entry.code = idx + 48;
			entry.length = 8;
			entry.extra_bits = 0;
			entry.dist = idx;
		} else if (idx <= 255) {
			entry.code = idx + 256;
			entry.length = 9;
			entry.extra_bits = 0;
			entry.dist = idx;
		} else if (idx <= 256) {
			entry.code = 0;
			entry.length = 7;
			entry.extra_bits = 0;
			entry.dist = idx;
		} else if (idx <= 264) {
			entry.code = idx - 256;
			entry.length = 7;
			entry.extra_bits = 0;
			entry.dist = idx - 254;
		} else if (idx <= 279) {
			entry.code = idx - 256;
			entry.length = 7;
			entry.extra_bits = (idx - 261) / 4;
			entry.dist = (1 << (entry.extra_bits + 2)) + 3 +
				((idx - 1) % 4) * (1 << entry.extra_bits);
		} else if (idx <= 284) {
			entry.code = idx - 88;
			entry.length = 8;
			entry.extra_bits = (idx - 261) / 4;
			entry.dist = (1 << (entry.extra_bits + 2)) + 3 +
				((idx - 1) % 4) * (1 << entry.extra_bits);
		} else if (idx <= 285) {
			entry.code = idx - 88;
			entry.length = 8;
			entry.extra_bits = 0;
			entry.dist = 258;
		} else if (idx <= 289) {
			entry.code = idx - 286;
			entry.length = 5;
			entry.extra_bits = 0;
			entry.dist = idx - 285;
		} else {
			entry.code = idx - 286;
			entry.length = 5;
			entry.extra_bits = (idx - 288) / 2;
			entry.dist = (1 << (entry.extra_bits + 1)) + 1 +
				(idx % 2) * (1 << entry.extra_bits);
		}

		writel(idx, tbl_reg);
		writel(entry.dist, tbl_reg + 0x4);
		writel((entry.code << 0) |
			(entry.length << 16) |
			(entry.extra_bits << 20),
			tbl_reg + 0x8);
	}
}

/** Inline CRC profile table entry */
struct gvt_comp_icrc_prof_ent {
	/** ID */
	enum gvt_comp_icrc_prof_id	id;
	/** byte swap on ingress data */
	uint8_t				ingress_byte_swap;
	/** bit swap on ingress data */
	uint8_t				ingress_bit_swap;
	/** byte swap on result */
	uint8_t				res_byte_swap;
	/** bit swap on result */
	uint8_t				res_bit_swap;
	/** perform one's completement on the result */
	uint8_t				res_one_completement;
	/** byte swap on iv data */
	uint8_t				iv_byte_swap;
	/** bit swap on iv data */
	uint8_t				iv_bit_swap;
	/** perform one's completement on the iv */
	uint8_t				iv_one_completement;
	/** index to crc header mask */
	unsigned int			start_mask_idx;
};

/** Inline CRC profile table entries */
static const struct gvt_comp_icrc_prof_ent icrc_prof_entries[] = {
	{
		.id = GVT_COMP_ICRC_PROF_ID_CRC16, /* CRC16-T10-DIF */
		.ingress_byte_swap = 0,
		.ingress_bit_swap = 0,
		.res_byte_swap = 1,
		.res_bit_swap = 0,
		.res_one_completement = 0,
		.iv_byte_swap = 1,
		.iv_bit_swap = 0,
		.iv_one_completement = 0,
		.start_mask_idx = 0,
	},
	{
		.id = GVT_COMP_ICRC_PROF_ID_CRC32, /* CRC32/CRC32C */
		.ingress_byte_swap = 0,
		.ingress_bit_swap = 1,
		.res_byte_swap = 0,
		.res_bit_swap = 1,
		.res_one_completement = 1,
		.iv_byte_swap = 0,
		.iv_bit_swap = 1,
		.iv_one_completement = 0,
		.start_mask_idx = 0,
	},
	{
		.id = GVT_COMP_ICRC_PROF_ID_ADLER, /* CHECKSUM_ADLER32 */
		.ingress_byte_swap = 1,
		.ingress_bit_swap = 0,
		.res_byte_swap = 1,
		.res_bit_swap = 0,
		.res_one_completement = 0,
		.iv_byte_swap = 1,
		.iv_bit_swap = 0,
		.iv_one_completement = 0,
		.start_mask_idx = 0,
	},
};

static void gvt_comp_icrc_prof_init(
			void __iomem *prof_reg,
			const struct gvt_comp_icrc_prof_ent *entries,
			unsigned int num_entries)
{
	for (; num_entries; num_entries--, entries++) {
		writel(entries->id, prof_reg);
		writel((entries->ingress_byte_swap ? (1 << 0) : 0) |
			(entries->ingress_bit_swap ? (1 << 1) : 0) |
			(entries->res_byte_swap ? (1 << 2) : 0) |
			(entries->res_bit_swap ? (1 << 3) : 0) |
			(entries->res_one_completement ? (1 << 4) : 0) |
			(entries->iv_byte_swap ? (1 << 5) : 0) |
			(entries->iv_bit_swap ? (1 << 6) : 0) |
			(entries->iv_one_completement ? (1 << 7) : 0) |
			(entries->start_mask_idx << 16),
			prof_reg + 4);
	}
}

void gvt_comp_engine_init(void __iomem *app_regs)
{
	void __iomem *comp_regs, *lz_cfg_reg;

	comp_regs = app_regs + GVT_COMP_REGS_OFF;

	lz_cfg_reg = comp_regs + GVT_COMP_LZ_CFG_OFF;
	writel(1UL << 1, lz_cfg_reg);

	gvt_comp_enc_prof_init(
		comp_regs, enc_prof_entries,
		ARRAY_SIZE(enc_prof_entries));
	gvt_comp_dec_prof_init(
		comp_regs, dec_prof_entries,
		ARRAY_SIZE(dec_prof_entries));
	gvt_comp_alg_ents_init(
		comp_regs, alg_entries,
		ARRAY_SIZE(alg_entries));
	gvt_comp_fmt_init(
		comp_regs, fmt_entries,
		ARRAY_SIZE(fmt_entries));
	gvt_comp_enc_out_init(
		comp_regs, enc_out_entries,
		ARRAY_SIZE(enc_out_entries));

	gvt_comp_huffman_static_tbl_init(comp_regs);

	gvt_comp_icrc_prof_init(app_regs + GVT_COMP_ICRC_PROF_OFF,
		icrc_prof_entries, ARRAY_SIZE(icrc_prof_entries));
}

/******************************************************************************
 ******************************************************************************/
static int pci_write_masked_config_dword(const struct pci_dev *dev, int where,
					u32 mask,
					u32 val)
{
	u32 temp;
	int rc;

	rc = pci_read_config_dword(dev, where, &temp);
	if (rc)
		return rc;

	temp = ((mask) & (val)) | ((~(mask)) & (temp));

	rc = pci_write_config_dword(dev, where, temp);
	return rc;
}

int gvt_unit_adapter_init(struct pci_dev *pdev)
{
	int rc;
	uint16_t vmid = PCI_DEVID(pdev->bus->number, pdev->devfn);

	/* i/o cache coherency */
	const unsigned int axcache_mask = 0x3ff;

	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_SMCC,
		GVT_ADAPTER_SMCC_CONF_SNOOP_OVR |
		GVT_ADAPTER_SMCC_CONF_SNOOP_ENABLE,
		GVT_ADAPTER_SMCC_CONF_SNOOP_OVR |
		GVT_ADAPTER_SMCC_CONF_SNOOP_ENABLE);
	if (rc)
		return rc;

	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_GENERIC_CONTROL_11,
		axcache_mask,
		axcache_mask);
	if (rc)
		return rc;

	/* Error tracking disable */
	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_SMCC_CONF_2,
		GVT_ADAPTER_SMCC_CONF_2_DIS_ERROR_TRACK,
		GVT_ADAPTER_SMCC_CONF_2_DIS_ERROR_TRACK);
	if (rc)
		return rc;

	/* ROB/WOB enable */
	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_GENERIC_CONTROL_19,
		GVT_ADAPTER_GEN_CTL_19_READ_ROB_SW_RESET |
		GVT_ADAPTER_GEN_CTL_19_WRITE_ROB_SW_RESET,
		GVT_ADAPTER_GEN_CTL_19_READ_ROB_SW_RESET |
		GVT_ADAPTER_GEN_CTL_19_WRITE_ROB_SW_RESET);
	if (rc)
		return rc;

	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_GENERIC_CONTROL_19,
		GVT_ADAPTER_GEN_CTL_19_READ_ROB_SW_RESET |
		GVT_ADAPTER_GEN_CTL_19_WRITE_ROB_SW_RESET,
		0);
	if (rc)
		return rc;

	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_GENERIC_CONTROL_19,
		GVT_ADAPTER_GEN_CTL_19_READ_ROB_EN |
		GVT_ADAPTER_GEN_CTL_19_READ_ROB_FORCE_INORDER |
		GVT_ADAPTER_GEN_CTL_19_WRITE_ROB_EN |
		GVT_ADAPTER_GEN_CTL_19_WRITE_ROB_FORCE_INORDER,
		GVT_ADAPTER_GEN_CTL_19_READ_ROB_EN |
		GVT_ADAPTER_GEN_CTL_19_WRITE_ROB_EN);
	if (rc)
		return rc;

	/* VMID enable */
	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_SMCC,
		GVT_ADAPTER_SMCC_CONF_VM_ID_IPA_OVERRIDE,
		GVT_ADAPTER_SMCC_CONF_VM_ID_IPA_OVERRIDE);
	if (rc)
		return rc;

	rc = pci_write_masked_config_dword(pdev,
		GVT_ADAPTER_SMCC,
		GVT_ADAPTER_SMCC_CONF_VM_ID_MASK,
		vmid << GVT_ADAPTER_SMCC_CONF_VM_ID_SHIFT);

	return rc;
}
