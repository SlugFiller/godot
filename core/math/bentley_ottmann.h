/**************************************************************************/
/*  bentley_ottmann.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef BENTLEY_OTTMANN_H
#define BENTLEY_OTTMANN_H

#include "core/math/vector2.h"
#include "core/templates/local_vector.h"
#include "thirdparty/misc/r128.h"

class BentleyOttmann {
public:
	BentleyOttmann(Vector<Vector2> p_edges, Vector<int> p_winding, bool p_winding_even_odd);
	Vector<Vector2> out_points;
	Vector<int32_t> out_triangles;

private:
	struct TreeNode {
		struct Version {
			uint32_t parent = 0;
			uint32_t left = 0;
			uint32_t right = 0;
			uint32_t prev = 0;
			uint32_t next = 0;
			bool is_heavy = false;
			int sum_value = 0;
			uint32_t size = 0;
		};
		Version current;
		Version previous;
		int self_value = 0;
		uint32_t index = 0;
		uint32_t element = 0;
		R128 version = R128_zero;
	};
	LocalVector<TreeNode> tree_nodes;

	struct ListNode {
		uint32_t anchor = 0;
		uint32_t prev = 0;
		uint32_t next = 0;
		uint32_t element = 0;
	};
	LocalVector<ListNode> list_nodes;

	struct Slice {
		R128 x;
		uint32_t points_tree;
		uint32_t vertical_tree;
		uint32_t check_list;
	};
	LocalVector<Slice> slices;
	uint32_t slices_tree;

	struct Point {
		uint32_t slice;
		R128 x;
		R128 y;
		uint32_t incoming_tree;
		uint32_t outgoing_tree;
		uint32_t used;
	};
	LocalVector<Point> points;

	struct Edge {
		uint32_t point_start;
		uint32_t point_end;
		uint32_t point_outgoing;
		uint32_t treenode_edges;
		uint32_t treenode_incoming;
		uint32_t treenode_outgoing;
		uint32_t listnode_incoming;
		uint32_t listnode_outgoing;
		uint32_t listnode_check;
		R128 x_next_check;
		R128 x_last_calculate;
		R128 y;
		R128 y_next;
		R128 dir_x;
		R128 dir_y;
		R128 step_y;
		R128 step_mod;
	};
	LocalVector<Edge> edges;
	uint32_t edges_tree;

	struct Vertical {
		R128 y;
		bool is_start;
	};
	LocalVector<Vertical> verticals;

	uint32_t add_slice(R128 p_x);
	uint32_t add_point(uint32_t p_slice, R128 p_y);
	uint32_t get_point_or_before(uint32_t p_slice, R128 p_y);
	uint32_t point_get_incoming_before(uint32_t p_point, uint32_t p_index);
	uint32_t point_get_outgoing_before(uint32_t p_point, uint32_t p_index);
	void add_edge(uint32_t p_point_start, uint32_t p_point_end, int p_winding);
	void add_vertical_edge(uint32_t p_slice, R128 p_y_start, R128 p_y_end);
	void edge_calculate_y(uint32_t p_edge, R128 p_x);
	uint32_t get_edge_before(R128 p_x, R128 p_y);
	uint32_t get_edge_before_end(R128 p_x, R128 p_y, R128 p_end_x, R128 p_end_y);
	uint32_t get_edge_before_previous(R128 p_x, R128 p_y);
	int edge_get_winding_previous(uint32_t p_treenode_edge, R128 p_x);
	void check_intersection(uint32_t p_treenode_edge, R128 p_x_min);

	uint32_t tree_create(uint32_t p_element = 0, int p_value = 0);
	void tree_insert(uint32_t p_insert_item, uint32_t p_insert_after, const R128 &p_version = R128_zero);
	void tree_remove(uint32_t p_remove_item, const R128 &p_version = R128_zero);
	void tree_rotate(uint32_t p_item, const R128 &p_version = R128_zero);
	void tree_swap(uint32_t p_item1, uint32_t p_item2, const R128 &p_version = R128_zero);
	void tree_version(uint32_t p_item, const R128 &p_version);
	void tree_index(uint32_t p_item);
	void tree_index_previous(uint32_t p_item, const R128 &p_version = R128_zero);

	uint32_t list_create(uint32_t p_element = 0);
	void list_insert(uint32_t p_insert_item, uint32_t p_list);
	void list_remove(uint32_t p_remove_item);
};

#endif // BENTLEY_OTTMANN_H
