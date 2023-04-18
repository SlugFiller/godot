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
		uint32_t version = 0;
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
		int32_t x[5];
		uint32_t points_tree;
		uint32_t vertical_tree;
		uint32_t check_list;
	};
	LocalVector<Slice> slices;
	uint32_t slices_tree;

	struct Point {
		uint32_t slice;
		int32_t x[5];
		int32_t y[5];
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
		uint32_t next_check;
		int32_t dir_x[5];
		int32_t dir_y[5];
		int32_t cross[10];
	};
	LocalVector<Edge> edges;
	uint32_t edges_tree;

	struct Vertical {
		int32_t y[5];
		bool is_start;
	};
	LocalVector<Vertical> verticals;

	uint32_t add_slice(const int32_t p_x[5]);
	uint32_t add_point(uint32_t p_slice, const int32_t p_y[5]);
	uint32_t get_point_before_edge(uint32_t p_slice, uint32_t p_edge, bool p_next_x);
	bool is_point_on_edge(uint32_t p_point, uint32_t p_edge, bool p_next_x);
	uint32_t point_get_incoming_before(uint32_t p_point, uint32_t p_index);
	uint32_t point_get_outgoing_before(uint32_t p_point, uint32_t p_index);
	void add_edge(uint32_t p_point_start, uint32_t p_point_end, int p_winding);
	void add_vertical_edge(uint32_t p_slice, const int32_t p_y_start[5], const int32_t p_y_end[5]);
	void edge_intersect_x(int32_t r_y[5], uint32_t p_edge, const int32_t p_x[5]);
	void edge_intersect_edge(int32_t r_y[5], uint32_t p_edge1, uint32_t p_edge2);
	uint32_t get_edge_before(const int32_t p_x[5], const int32_t p_y[5]);
	uint32_t get_edge_before_end(const int32_t p_x[5], const int32_t p_y[5], const int32_t p_end_x[5], const int32_t p_end_y[5]);
	uint32_t get_edge_before_previous(uint32_t p_slice, const int32_t p_y[5]);
	int edge_get_winding_previous(uint32_t p_treenode_edge, uint32_t p_version);
	void check_intersection(uint32_t p_treenode_edge);

	uint32_t tree_create(uint32_t p_element = 0, int p_value = 0);
	void tree_insert(uint32_t p_insert_item, uint32_t p_insert_after, uint32_t p_version = 0);
	void tree_remove(uint32_t p_remove_item, uint32_t p_version = 0);
	void tree_rotate(uint32_t p_item, uint32_t p_version = 0);
	void tree_swap(uint32_t p_item1, uint32_t p_item2, uint32_t p_version = 0);
	void tree_version(uint32_t p_item, uint32_t p_version);
	void tree_index(uint32_t p_item);
	void tree_index_previous(uint32_t p_item, uint32_t p_version = 0);

	uint32_t list_create(uint32_t p_element = 0);
	void list_insert(uint32_t p_insert_item, uint32_t p_list);
	void list_remove(uint32_t p_remove_item);
};

#endif // BENTLEY_OTTMANN_H
