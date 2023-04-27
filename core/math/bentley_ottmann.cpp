/**************************************************************************/
/*  bentley_ottmann.cpp                                                   */
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

#include "bentley_ottmann.h"
#include "core/math/rect2.h"

#define EXP_MIN -65536

thread_local LocalVector<BentleyOttmann::TreeNode> BentleyOttmann::tree_nodes;
thread_local LocalVector<BentleyOttmann::ListNode> BentleyOttmann::list_nodes;
thread_local LocalVector<BentleyOttmann::Slice> BentleyOttmann::slices;
thread_local LocalVector<BentleyOttmann::Point> BentleyOttmann::points;
thread_local LocalVector<BentleyOttmann::Edge> BentleyOttmann::edges;
thread_local LocalVector<BentleyOttmann::Vertical> BentleyOttmann::verticals;

BentleyOttmann::BentleyOttmann(Vector<Vector2> p_edges, Vector<int> p_winding, bool p_winding_even_odd) {
	tree_nodes.clear();
	list_nodes.clear();
	slices.clear();
	points.clear();
	edges.clear();
	verticals.clear();
	// The cost of an explicit nil node is lower than having a special nil value.
	// This also ensures that tree_nodes[0].element is 0 instead of a null pointer exception.
	TreeNode nil_node;
	tree_nodes.push_back(nil_node);
	edges_tree = tree_create();
	slices_tree = tree_create();
	int winding_mask = p_winding_even_odd ? 1 : -1;

	ERR_FAIL_COND(p_edges.size() & 1);
	ERR_FAIL_COND((p_edges.size() >> 1) != p_winding.size());
	if (p_edges.size() < 1) {
		return;
	}
	int x_exp = EXP_MIN;
	int y_exp = EXP_MIN;
	for (int i = 0; i < p_edges.size(); i++) {
		if (isnormal(p_edges[i].x)) {
			int exp;
			frexp(p_edges[i].x, &exp);
			if (x_exp < exp) {
				x_exp = exp;
			}
		}
		if (isnormal(p_edges[i].y)) {
			int exp;
			frexp(p_edges[i].y, &exp);
			if (y_exp < exp) {
				y_exp = exp;
			}
		}
	}
	if (x_exp == EXP_MIN) {
		x_exp = 0;
	} else {
		x_exp -= 21;
	}
	if (y_exp == EXP_MIN) {
		y_exp = 0;
	} else {
		y_exp -= 21;
	}
	for (int i = 0, j = 0; i < p_winding.size(); i++, j += 2) {
		if (!p_winding[i]) {
			// Zero-winding edges are used internally for concave shapes and holes.
			// Therefore, don't allow them as input.
			continue;
		}
		int64_t start_x = static_cast<int64_t>(ldexp(p_edges[j].x, -x_exp));
		int64_t start_y = static_cast<int64_t>(ldexp(p_edges[j].y, -y_exp));
		int64_t end_x = static_cast<int64_t>(ldexp(p_edges[j + 1].x, -x_exp));
		int64_t end_y = static_cast<int64_t>(ldexp(p_edges[j + 1].y, -y_exp));
		if (start_x < end_x) {
			add_edge(add_point(add_slice(start_x), start_y), add_point(add_slice(end_x), end_y), p_winding[i]);
		} else if (start_x > end_x) {
			add_edge(add_point(add_slice(end_x), end_y), add_point(add_slice(start_x), start_y), -p_winding[i]);
		} else if (start_y < end_y) {
			add_vertical_edge(add_slice(start_x), start_y, end_y);
		} else if (start_y > end_y) {
			add_vertical_edge(add_slice(start_x), end_y, start_y);
		}
	}

	LocalVector<uint32_t> triangles;
	uint32_t incoming_list = list_create();
	uint32_t outgoing_list = list_create();

	uint32_t slice_iter = tree_nodes[slices_tree].current.next;
	while (slice_iter != slices_tree) {
		uint32_t slice = tree_nodes[slice_iter].element;

		{
			// Remove edges ending at this slice
			uint32_t check_iter = list_nodes[slices[slice].check_list].next;
			while (check_iter != slices[slice].check_list) {
				DEV_ASSERT(edges[list_nodes[check_iter].element].next_check == slice);
				uint32_t check_iter_next = list_nodes[check_iter].next;
				if (points[edges[list_nodes[check_iter].element].point_end].slice == slice) {
					uint32_t treenode_edge_prev = tree_nodes[edges[list_nodes[check_iter].element].treenode_edges].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
					list_insert(edges[list_nodes[check_iter].element].listnode_incoming, incoming_list);
					tree_remove<false>(edges[list_nodes[check_iter].element].treenode_edges, slice);
					list_remove(check_iter);
				}
				check_iter = check_iter_next;
			}
		}

		{
			// Mark intersection of passthrough edges with vertical edges
			uint32_t vertical_iter = tree_nodes[slices[slice].vertical_tree].current.next;
			while (vertical_iter != slices[slice].vertical_tree) {
				DEV_ASSERT(verticals[tree_nodes[vertical_iter].element].is_start);
				uint32_t treenode_edge = get_edge_before(slices[slice].x, verticals[tree_nodes[vertical_iter].element].y);
				vertical_iter = tree_nodes[vertical_iter].current.next;
				DEV_ASSERT(vertical_iter != slices[slice].vertical_tree);
				DEV_ASSERT(!verticals[tree_nodes[vertical_iter].element].is_start);
				while (tree_nodes[treenode_edge].current.next != edges_tree) {
					treenode_edge = tree_nodes[treenode_edge].current.next;
					const Edge &edge = edges[tree_nodes[treenode_edge].element];
					if (verticals[tree_nodes[vertical_iter].element].y * edge.dir_x + slices[slice].x * edge.dir_y <= edge.cross) {
						break;
					}
					int64_t y = edge_intersect_x(tree_nodes[treenode_edge].element, slices[slice].x);
					add_point(slice, y);
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_incoming, incoming_list);
					list_insert(edges[tree_nodes[treenode_edge].element].listnode_outgoing, outgoing_list);
					DEV_ASSERT(is_point_on_edge(add_point(slice, y), tree_nodes[treenode_edge].element, false));
				}
				vertical_iter = tree_nodes[vertical_iter].current.next;
			}
		}

		{
			// Add edges starting at this slice
			uint32_t check_iter = list_nodes[slices[slice].check_list].next;
			while (check_iter != slices[slice].check_list) {
				DEV_ASSERT(edges[list_nodes[check_iter].element].next_check == slice);
				if (points[edges[list_nodes[check_iter].element].point_start].slice == slice) {
					uint32_t treenode_edge = get_edge_before_end(slices[slice].x, points[edges[list_nodes[check_iter].element].point_start].y, points[edges[list_nodes[check_iter].element].point_end].x, points[edges[list_nodes[check_iter].element].point_end].y);
					list_insert(edges[list_nodes[check_iter].element].listnode_outgoing, outgoing_list);
					tree_insert<false>(edges[list_nodes[check_iter].element].treenode_edges, treenode_edge, slice);
					if (treenode_edge != edges_tree) {
						edges[tree_nodes[treenode_edge].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge].element].listnode_check, slices[slice].check_list);
					}
				}
				check_iter = list_nodes[check_iter].next;
			}
		}

		{
			// Check order changes of edges, and mark as intersections
			int64_t x = slices[slice].x + 1;
			while (list_nodes[slices[slice].check_list].next != slices[slice].check_list) {
				uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
				DEV_ASSERT(edges[edge].next_check == slice);
				// Reset the next check of the checked edge to its end point.
				// This will be reduced to the nearest intersection if one is found.
				edges[edge].next_check = points[edges[edge].point_end].slice;
				list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
				uint32_t treenode_edge_next = tree_nodes[edges[edge].treenode_edges].current.next;
				if (treenode_edge_next == edges_tree) {
					continue;
				}
				uint32_t edge_next = tree_nodes[treenode_edge_next].element;
				Edge &edge1 = edges[edge];
				Edge &edge2 = edges[edge_next];
				if (edge1.max_y < edge2.min_y) {
					continue;
				}
				if ((x * edge2.dir_y + edge2.cross) * edge1.dir_x >= (x * edge1.dir_y + edge1.cross) * edge2.dir_x) {
					continue;
				}
				add_point(slice, edge_intersect_edge(edge, edge_next));
				if (tree_nodes[edges[edge].treenode_edges].self_value == 0) {
					tree_remove<false>(edges[edge].treenode_edges, slice);
					if (points[edges[edge].point_start].slice != slice) {
						list_insert(edges[edge].listnode_incoming, incoming_list);
					}
					if (points[edges[edge_next].point_start].slice != slice) {
						list_insert(edges[edge_next].listnode_incoming, incoming_list);
					}
					list_insert(edges[edge_next].listnode_outgoing, outgoing_list);
					list_remove(edges[edge].listnode_check);
					uint32_t treenode_edge_prev = tree_nodes[treenode_edge_next].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
				} else if (tree_nodes[treenode_edge_next].self_value == 0) {
					tree_remove<false>(treenode_edge_next, slice);
					if (points[edges[edge].point_start].slice != slice) {
						list_insert(edges[edge].listnode_incoming, incoming_list);
					}
					if (points[edges[edge_next].point_start].slice != slice) {
						list_insert(edges[edge_next].listnode_incoming, incoming_list);
					}
					list_insert(edges[edge].listnode_outgoing, outgoing_list);
					list_remove(edges[edge_next].listnode_check);
					edges[edge].next_check = slice;
					list_insert(edges[edge].listnode_check, slices[slice].check_list);
				} else {
					tree_swap<false>(edges[edge].treenode_edges, treenode_edge_next, slice);
					if (points[edges[edge].point_start].slice != slice) {
						list_insert(edges[edge].listnode_incoming, incoming_list);
					}
					if (points[edges[edge_next].point_start].slice != slice) {
						list_insert(edges[edge_next].listnode_incoming, incoming_list);
					}
					list_insert(edges[edge].listnode_outgoing, outgoing_list);
					list_insert(edges[edge_next].listnode_outgoing, outgoing_list);
					edges[edge].next_check = slice;
					list_insert(edges[edge].listnode_check, slices[slice].check_list);
					uint32_t treenode_edge_prev = tree_nodes[treenode_edge_next].current.prev;
					if (treenode_edge_prev != edges_tree) {
						edges[tree_nodes[treenode_edge_prev].element].next_check = slice;
						list_insert(edges[tree_nodes[treenode_edge_prev].element].listnode_check, slices[slice].check_list);
					}
				}
			}
		}

		{
			// Add incoming edges to points
			while (list_nodes[incoming_list].next != incoming_list) {
				uint32_t edge = list_nodes[list_nodes[incoming_list].next].element;
				list_remove(list_nodes[incoming_list].next);
				tree_index_previous(edges[edge].treenode_edges, slice);
				uint32_t treenode_point = get_point_before_edge(slice, edge, false);
				if (treenode_point == slices[slice].points_tree || (tree_nodes[treenode_point].current.next != slices[slice].points_tree && !is_point_on_edge(tree_nodes[treenode_point].element, edge, false) && (edges[edge].dir_y > 0 || is_point_on_edge(tree_nodes[tree_nodes[treenode_point].current.next].element, edge, false)))) {
					treenode_point = tree_nodes[treenode_point].current.next;
				}
				DEV_ASSERT(treenode_point != slices[slice].points_tree);
				tree_insert<true>(edges[edge].treenode_incoming, point_get_incoming_before(tree_nodes[treenode_point].element, tree_nodes[edges[edge].treenode_edges].previous.index));
			}
		}

		{
			// Add outgoing edges to points
			while (list_nodes[outgoing_list].next != outgoing_list) {
				uint32_t edge = list_nodes[list_nodes[outgoing_list].next].element;
				list_remove(list_nodes[outgoing_list].next);
				tree_index(edges[edge].treenode_edges);
				uint32_t treenode_point = get_point_before_edge(slice, edge, true);
				if (treenode_point == slices[slice].points_tree || (tree_nodes[treenode_point].current.next != slices[slice].points_tree && !is_point_on_edge(tree_nodes[treenode_point].element, edge, true) && (edges[edge].dir_y < 0 || is_point_on_edge(tree_nodes[tree_nodes[treenode_point].current.next].element, edge, true)))) {
					treenode_point = tree_nodes[treenode_point].current.next;
				}
				DEV_ASSERT(treenode_point != slices[slice].points_tree);
				tree_insert<true>(edges[edge].treenode_outgoing, point_get_outgoing_before(tree_nodes[treenode_point].element, tree_nodes[edges[edge].treenode_edges].current.index));
			}
		}

		{
			// Erase unused points
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t point_iter_next = tree_nodes[point_iter].current.next;
				if (tree_nodes[points[point].incoming_tree].current.next == points[point].incoming_tree && tree_nodes[points[point].outgoing_tree].current.next == points[point].outgoing_tree) {
					tree_remove<true>(point_iter);
				}
				point_iter = point_iter_next;
			}
		}

		{
			// Force edges going through a point to treat it as intersection
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				// Edges are currently sorted by their y at the next x. To get their sorting
				// by the y at the current x, we need to use the previous tree
				uint32_t treenode_edge = get_edge_before_previous(slice, points[point].y);
				// Find first edge coinciding with the point
				while (treenode_edge != edges_tree && is_point_on_edge(point, tree_nodes[treenode_edge].element, false)) {
					if (tree_nodes[treenode_edge].version == slice) {
						treenode_edge = tree_nodes[treenode_edge].previous.prev;
					} else {
						treenode_edge = tree_nodes[treenode_edge].current.prev;
					}
				}
				if (tree_nodes[treenode_edge].version == slice) {
					treenode_edge = tree_nodes[treenode_edge].previous.next;
				} else {
					treenode_edge = tree_nodes[treenode_edge].current.next;
				}
				while (treenode_edge != edges_tree && is_point_on_edge(point, tree_nodes[treenode_edge].element, false)) {
					// If the edge hasn't been already added as either incoming or outgoing
					if (tree_nodes[edges[tree_nodes[treenode_edge].element].treenode_incoming].current.parent == 0 && tree_nodes[edges[tree_nodes[treenode_edge].element].treenode_outgoing].current.parent == 0) {
						tree_index_previous(treenode_edge, slice);
						tree_insert<true>(edges[tree_nodes[treenode_edge].element].treenode_incoming, point_get_incoming_before(point, tree_nodes[treenode_edge].previous.index));
						if (tree_nodes[treenode_edge].current.parent != 0) {
							// If the edge wasn't removed this slice, add outgoing too
							tree_index(treenode_edge);
							tree_insert<true>(edges[tree_nodes[treenode_edge].element].treenode_outgoing, point_get_outgoing_before(point, tree_nodes[treenode_edge].current.index));
						}
					}
					if (tree_nodes[treenode_edge].version == slice) {
						treenode_edge = tree_nodes[treenode_edge].previous.next;
					} else {
						treenode_edge = tree_nodes[treenode_edge].current.next;
					}
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Produce triangles
			int winding = 0;
			uint32_t treenode_edge_previous = edges_tree;
			uint32_t point_previous = 0;
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t treenode_edge_before;
				if (tree_nodes[points[point].incoming_tree].current.next != points[point].incoming_tree) {
					uint32_t treenode_edge_first = edges[tree_nodes[tree_nodes[points[point].incoming_tree].current.next].element].treenode_edges;
					if (tree_nodes[treenode_edge_first].version == slice) {
						treenode_edge_before = tree_nodes[treenode_edge_first].previous.prev;
					} else {
						treenode_edge_before = tree_nodes[treenode_edge_first].current.prev;
					}
				} else {
					treenode_edge_before = get_edge_before_previous(slice, points[point].y);
				}
				if (treenode_edge_before == treenode_edge_previous) {
					if (winding & winding_mask) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(point_previous);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slice) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
				} else {
					treenode_edge_previous = treenode_edge_before;
					winding = edge_get_winding_previous(treenode_edge_previous, slice);
					if (winding & winding_mask) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(edges[tree_nodes[treenode_edge_previous].element].point_outgoing);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slice) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
				}
				uint32_t edge_incoming_iter = tree_nodes[points[point].incoming_tree].current.next;
				while (edge_incoming_iter != points[point].incoming_tree) {
					DEV_ASSERT(edges[tree_nodes[edge_incoming_iter].element].treenode_edges == (tree_nodes[treenode_edge_previous].version == slice ? tree_nodes[treenode_edge_previous].previous.next : tree_nodes[treenode_edge_previous].current.next));
					treenode_edge_previous = edges[tree_nodes[edge_incoming_iter].element].treenode_edges;
					winding += tree_nodes[treenode_edge_previous].self_value;
					if (winding & winding_mask) {
						DEV_ASSERT(treenode_edge_previous != edges_tree);
						triangles.push_back(edges[tree_nodes[treenode_edge_previous].element].point_outgoing);
						triangles.push_back(point);
						if (tree_nodes[treenode_edge_previous].version == slice) {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].previous.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].previous.next].element].point_outgoing);
						} else {
							DEV_ASSERT(tree_nodes[treenode_edge_previous].current.next != edges_tree);
							triangles.push_back(edges[tree_nodes[tree_nodes[treenode_edge_previous].current.next].element].point_outgoing);
						}
					}
					edge_incoming_iter = tree_nodes[edge_incoming_iter].current.next;
				}
				point_previous = point;
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Set outgoing points for subsequent triangle production
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t edge_outgoing_iter = tree_nodes[points[point].outgoing_tree].current.next;
				while (edge_outgoing_iter != points[point].outgoing_tree) {
					edges[tree_nodes[edge_outgoing_iter].element].point_outgoing = point;
					edge_outgoing_iter = tree_nodes[edge_outgoing_iter].current.next;
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Add helper edges
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				// Concave point or hole in the x direction
				// Has two connected points with equal or lower x. Add an edge
				// ensuring those points are not connected to each other.
				if (tree_nodes[points[point].outgoing_tree].current.next == points[point].outgoing_tree) {
					uint32_t treenode_edge_before = get_edge_before(slices[slice].x, points[point].y);
					if (treenode_edge_before != edges_tree && tree_nodes[treenode_edge_before].current.next != edges_tree) {
						DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
						if (points[edges[tree_nodes[treenode_edge_before].element].point_end].x < points[edges[tree_nodes[tree_nodes[treenode_edge_before].current.next].element].point_end].x) {
							add_edge(point, edges[tree_nodes[treenode_edge_before].element].point_end, 0);
						} else {
							add_edge(point, edges[tree_nodes[tree_nodes[treenode_edge_before].current.next].element].point_end, 0);
						}
						// Adding the edge at the current slice will cause it to be added to the check list.
						// Remove it, and add it to the point's outgoing edges.
						DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
						uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
						tree_insert<false>(edges[edge].treenode_edges, treenode_edge_before, slice);
						tree_insert<true>(edges[edge].treenode_outgoing, points[point].outgoing_tree);
						edges[edge].next_check = points[edges[edge].point_end].slice;
						list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
						DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
					}
				}
				// Concave points in the y direction
				// A quad formed by the edges connected to this point and the next edges
				// above or below is concave. Add an edge to split it into triangles.
				if (tree_nodes[points[point].outgoing_tree].current.next != points[point].outgoing_tree) {
					{
						uint32_t edge_first = tree_nodes[tree_nodes[points[point].outgoing_tree].current.next].element;
						uint32_t treenode_edge_other = tree_nodes[edges[edge_first].treenode_edges].current.prev;
						if (treenode_edge_other != edges_tree && edges[edge_first].point_start == point) {
							uint32_t point_edge_end = edges[edge_first].point_end;
							uint32_t point_other_outgoing = edges[tree_nodes[treenode_edge_other].element].point_outgoing;
							if ((points[point].x - points[point_other_outgoing].x) * (points[point_edge_end].y - points[point_other_outgoing].y) > (points[point].y - points[point_other_outgoing].y) * (points[point_edge_end].x - points[point_other_outgoing].x)) {
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
								add_edge(point, edges[tree_nodes[treenode_edge_other].element].point_end, 0);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
								uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
								tree_insert<false>(edges[edge].treenode_edges, treenode_edge_other, slice);
								tree_insert<true>(edges[edge].treenode_outgoing, points[point].outgoing_tree);
								edges[edge].next_check = points[edges[edge].point_end].slice;
								list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
							}
						}
					}
					{
						uint32_t edge_last = tree_nodes[tree_nodes[points[point].outgoing_tree].current.prev].element;
						uint32_t treenode_edge_other = tree_nodes[edges[edge_last].treenode_edges].current.next;
						if (treenode_edge_other != edges_tree && edges[edge_last].point_start == point) {
							uint32_t point_edge_end = edges[edge_last].point_end;
							uint32_t point_other_outgoing = edges[tree_nodes[treenode_edge_other].element].point_outgoing;
							if ((points[point].x - points[point_other_outgoing].x) * (points[point_edge_end].y - points[point_other_outgoing].y) < (points[point].y - points[point_other_outgoing].y) * (points[point_edge_end].x - points[point_other_outgoing].x)) {
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
								add_edge(point, edges[tree_nodes[treenode_edge_other].element].point_end, 0);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next != slices[slice].check_list);
								uint32_t edge = list_nodes[list_nodes[slices[slice].check_list].next].element;
								tree_insert<false>(edges[edge].treenode_edges, edges[edge_last].treenode_edges, slice);
								tree_insert<true>(edges[edge].treenode_outgoing, edges[edge_last].treenode_outgoing);
								edges[edge].next_check = points[edges[edge].point_end].slice;
								list_insert(edges[edge].listnode_check, slices[points[edges[edge].point_end].slice].check_list);
								DEV_ASSERT(list_nodes[slices[slice].check_list].next == slices[slice].check_list);
							}
						}
					}
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Check for possible next intersections
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				uint32_t edge_outgoing_iter = tree_nodes[points[point].outgoing_tree].current.next;
				if (edge_outgoing_iter != points[point].outgoing_tree) {
					uint32_t treenode_edge = tree_nodes[edges[tree_nodes[edge_outgoing_iter].element].treenode_edges].current.prev;
					if (treenode_edge != edges_tree) {
						check_intersection(treenode_edge);
					}
				}
				while (edge_outgoing_iter != points[point].outgoing_tree) {
					uint32_t treenode_edge = edges[tree_nodes[edge_outgoing_iter].element].treenode_edges;
					if (tree_nodes[treenode_edge].current.next != edges_tree) {
						check_intersection(treenode_edge);
					}
					edge_outgoing_iter = tree_nodes[edge_outgoing_iter].current.next;
				}
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		{
			// Cleanup
			uint32_t point_iter = tree_nodes[slices[slice].points_tree].current.next;
			while (point_iter != slices[slice].points_tree) {
				uint32_t point = tree_nodes[point_iter].element;
				// Need to clear the incoming and outgoing, so the same edges
				// can be added to incoming and outgoing in subsequent slices
				tree_clear<true>(points[point].incoming_tree);
				tree_clear<true>(points[point].outgoing_tree);
				point_iter = tree_nodes[point_iter].current.next;
			}
		}

		DEV_ASSERT(list_nodes[incoming_list].next == incoming_list);
		DEV_ASSERT(list_nodes[outgoing_list].next == outgoing_list);

		slice_iter = tree_nodes[slice_iter].current.next;
	}

	DEV_ASSERT(tree_nodes[edges_tree].current.right == 0);

	// Optimize points and flush to final buffers
	for (uint32_t i = 0; i < points.size(); i++) {
		points[i].used = 0;
	}
	DEV_ASSERT((triangles.size() % 3) == 0);
	for (uint32_t i = 0; i < triangles.size();) {
		if (triangles[i] == triangles[i + 1] || triangles[i] == triangles[i + 2] || triangles[i + 1] == triangles[i + 2]) {
			i += 3;
			continue;
		}
		for (uint32_t j = 0; j < 3; i++, j++) {
			if (!points[triangles[i]].used) {
				out_points.push_back(Vector2(ldexp(static_cast<real_t>(points[triangles[i]].x), x_exp), ldexp(static_cast<real_t>(points[triangles[i]].y), y_exp)));
				points[triangles[i]].used = out_points.size();
			}
			out_triangles.push_back(points[triangles[i]].used - 1);
		}
	}
}

uint32_t BentleyOttmann::add_slice(int64_t p_x) {
	uint32_t insert_after = slices_tree;
	uint32_t current = tree_nodes[slices_tree].current.right;
	if (current) {
		while (true) {
			int64_t x = p_x - slices[tree_nodes[current].element].x;
			if (x < 0) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				insert_after = tree_nodes[current].current.prev;
				break;
			}
			if (x > 0) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				insert_after = current;
				break;
			}
			return tree_nodes[current].element;
		}
	}
	Slice slice;
	slice.x = p_x;
	slice.points_tree = tree_create();
	slice.vertical_tree = tree_create();
	slice.check_list = list_create();
	tree_insert<true>(tree_create(slices.size()), insert_after);
	slices.push_back(slice);
	return slices.size() - 1;
}

uint32_t BentleyOttmann::add_point(uint32_t p_slice, int64_t p_y) {
	uint32_t insert_after = slices[p_slice].points_tree;
	uint32_t current = tree_nodes[slices[p_slice].points_tree].current.right;
	if (current) {
		while (true) {
			int64_t y = p_y - points[tree_nodes[current].element].y;
			if (y < 0) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				insert_after = tree_nodes[current].current.prev;
				break;
			}
			if (y > 0) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				insert_after = current;
				break;
			}
			return tree_nodes[current].element;
		}
	}
	Point point;
	point.slice = p_slice;
	point.x = slices[p_slice].x;
	point.y = p_y;
	point.incoming_tree = tree_create();
	point.outgoing_tree = tree_create();
	tree_insert<true>(tree_create(points.size()), insert_after);
	points.push_back(point);
	return points.size() - 1;
}

uint32_t BentleyOttmann::get_point_before_edge(uint32_t p_slice, uint32_t p_edge, bool p_next_x) {
	uint32_t current = tree_nodes[slices[p_slice].points_tree].current.right;
	if (!current) {
		return slices[p_slice].points_tree;
	}
	const Edge &edge = edges[p_edge];
	int64_t x = slices[p_slice].x;
	if (p_next_x) {
		x++;
	}
	while (true) {
		int64_t cross = points[tree_nodes[current].element].y * edge.dir_x - x * edge.dir_y - edge.cross;
		if (cross > 0) {
			if (tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
		if (cross < 0 && tree_nodes[current].current.right) {
			current = tree_nodes[current].current.right;
			continue;
		}
		return current;
	}
}

bool BentleyOttmann::is_point_on_edge(uint32_t p_point, uint32_t p_edge, bool p_next_x) {
	const Edge &edge = edges[p_edge];
	int64_t x = points[p_point].x;
	if (p_next_x) {
		x++;
	}
	int64_t mod = (points[p_point].y * edge.dir_x - x * edge.dir_y - edge.cross) << 1;
	return mod <= edge.dir_x && mod + edge.dir_x > 0;
}

uint32_t BentleyOttmann::point_get_incoming_before(uint32_t p_point, uint32_t p_index) {
	uint32_t current = tree_nodes[points[p_point].incoming_tree].current.right;
	if (!current) {
		return points[p_point].incoming_tree;
	}
	while (true) {
		uint32_t index = tree_nodes[edges[tree_nodes[current].element].treenode_edges].previous.index;
		if (p_index > index) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (p_index < index && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::point_get_outgoing_before(uint32_t p_point, uint32_t p_index) {
	uint32_t current = tree_nodes[points[p_point].outgoing_tree].current.right;
	if (!current) {
		return points[p_point].outgoing_tree;
	}
	while (true) {
		uint32_t index = tree_nodes[edges[tree_nodes[current].element].treenode_edges].current.index;
		if (p_index > index) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (p_index < index && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

void BentleyOttmann::add_edge(uint32_t p_point_start, uint32_t p_point_end, int p_winding) {
	Edge edge;
	edge.point_start = edge.point_outgoing = p_point_start;
	edge.point_end = p_point_end;
	edge.treenode_edges = tree_create(edges.size(), p_winding);
	edge.treenode_incoming = tree_create(edges.size());
	edge.treenode_outgoing = tree_create(edges.size());
	edge.listnode_incoming = list_create(edges.size());
	edge.listnode_outgoing = list_create(edges.size());
	edge.listnode_check = list_create(edges.size());
	edge.dir_x = points[p_point_end].x - points[p_point_start].x;
	edge.dir_y = points[p_point_end].y - points[p_point_start].y;
	if (edge.dir_y >= 0) {
		edge.min_y = points[p_point_start].y;
		edge.max_y = points[p_point_end].y;
	} else {
		edge.min_y = points[p_point_end].y;
		edge.max_y = points[p_point_start].y;
	}
	DEV_ASSERT(edge.dir_x > 0);
	edge.next_check = points[p_point_start].slice;
	edge.cross = points[p_point_start].y * edge.dir_x - points[p_point_start].x * edge.dir_y;
	edges.push_back(edge);
	list_insert(edge.listnode_check, slices[points[p_point_start].slice].check_list);
}

void BentleyOttmann::add_vertical_edge(uint32_t p_slice, int64_t p_y_start, int64_t p_y_end) {
	uint32_t start;
	uint32_t current = tree_nodes[slices[p_slice].vertical_tree].current.right;
	if (!current) {
		Vertical vertical;
		vertical.y = p_y_start;
		vertical.is_start = true;
		start = tree_create(verticals.size());
		verticals.push_back(vertical);
		tree_insert<true>(start, slices[p_slice].vertical_tree);
	} else {
		while (true) {
			int64_t y = p_y_start - verticals[tree_nodes[current].element].y;
			if (y < 0) {
				if (tree_nodes[current].current.left) {
					current = tree_nodes[current].current.left;
					continue;
				}
				if (verticals[tree_nodes[current].element].is_start) {
					Vertical vertical;
					vertical.y = p_y_start;
					vertical.is_start = true;
					start = tree_create(verticals.size());
					verticals.push_back(vertical);
					tree_insert<true>(start, tree_nodes[current].current.prev);
				} else {
					start = tree_nodes[current].current.prev;
				}
				break;
			}
			if (y > 0) {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
				if (!verticals[tree_nodes[current].element].is_start) {
					Vertical vertical;
					vertical.y = p_y_start;
					vertical.is_start = true;
					start = tree_create(verticals.size());
					verticals.push_back(vertical);
					tree_insert<true>(start, current);
				} else {
					start = current;
				}
				break;
			}
			if (verticals[tree_nodes[current].element].is_start) {
				start = current;
			} else {
				start = tree_nodes[current].current.prev;
			}
			break;
		}
	}
	while (tree_nodes[start].current.next != slices[p_slice].vertical_tree) {
		int64_t y = p_y_end - verticals[tree_nodes[tree_nodes[start].current.next].element].y;
		if (y < 0 || (y == 0 && !verticals[tree_nodes[tree_nodes[start].current.next].element].is_start)) {
			break;
		}
		tree_remove<true>(tree_nodes[start].current.next);
	}
	if (tree_nodes[start].current.next == slices[p_slice].vertical_tree || verticals[tree_nodes[tree_nodes[start].current.next].element].is_start) {
		Vertical vertical;
		vertical.y = p_y_end;
		vertical.is_start = false;
		tree_insert<true>(tree_create(verticals.size()), start);
		verticals.push_back(vertical);
	}
}

int64_t BentleyOttmann::edge_intersect_x(uint32_t p_edge, int64_t p_x) {
	const Edge &edge = edges[p_edge];
	int64_t total = p_x * edge.dir_y + edge.cross;
	int64_t y = total / edge.dir_x;
	int64_t mod = total % edge.dir_x;
	if (mod < 0) {
		mod += edge.dir_x;
		y--;
	}
	if ((mod << 1) >= edge.dir_x) {
		y++;
	}
	return y;
}

int64_t BentleyOttmann::edge_intersect_edge(uint32_t p_edge1, uint32_t p_edge2) {
	const Edge &edge1 = edges[p_edge1];
	const Edge &edge2 = edges[p_edge2];
	int64_t total = edge2.cross * edge1.dir_y - edge1.cross * edge2.dir_y;
	int64_t factor = edge1.dir_y * edge2.dir_x - edge2.dir_y * edge1.dir_x;
	int64_t y = total / factor;
	int64_t mod = total % factor;
	if (mod < 0) {
		mod += factor;
		y--;
	}
	if ((mod << 1) >= factor) {
		y++;
	}
	return y;
}

uint32_t BentleyOttmann::get_edge_before(int64_t p_x, int64_t p_y) {
	uint32_t current = tree_nodes[edges_tree].current.right;
	if (!current) {
		return edges_tree;
	}
	while (true) {
		const Edge &edge = edges[tree_nodes[current].element];
		int64_t cross = p_y * edge.dir_x - p_x * edge.dir_y - edge.cross;
		if (cross > 0) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (cross < 0 && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::get_edge_before_end(int64_t p_x, int64_t p_y, int64_t p_end_x, int64_t p_end_y) {
	uint32_t current = tree_nodes[edges_tree].current.right;
	if (!current) {
		return edges_tree;
	}
	int64_t a_x = p_end_x - p_x;
	int64_t a_y = p_end_y - p_y;
	while (true) {
		const Edge &edge = edges[tree_nodes[current].element];
		int64_t cross = p_y * edge.dir_x - p_x * edge.dir_y - edge.cross;
		if (cross > 0) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (cross < 0) {
			if (tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
		// This is a best-effort attempt, since edges are not guaranteed
		// to be sorted by end.
		cross = a_y * (points[edges[tree_nodes[current].element].point_end].x - p_x) - a_x * (points[edges[tree_nodes[current].element].point_end].y - p_y);
		if (cross > 0) {
			if (tree_nodes[current].current.right) {
				current = tree_nodes[current].current.right;
				continue;
			}
			return current;
		}
		if (cross < 0 && tree_nodes[current].current.left) {
			current = tree_nodes[current].current.left;
			continue;
		}
		return tree_nodes[current].current.prev;
	}
}

uint32_t BentleyOttmann::get_edge_before_previous(uint32_t p_slice, int64_t p_y) {
	uint32_t current;
	if (tree_nodes[edges_tree].version == p_slice) {
		current = tree_nodes[edges_tree].previous.right;
	} else {
		current = tree_nodes[edges_tree].current.right;
	}
	if (!current) {
		return edges_tree;
	}
	while (true) {
		const Edge &edge = edges[tree_nodes[current].element];
		int64_t cross = p_y * edge.dir_x - slices[p_slice].x * edge.dir_y - edge.cross;
		if (cross > 0) {
			if (tree_nodes[current].version == p_slice) {
				if (tree_nodes[current].previous.right) {
					current = tree_nodes[current].previous.right;
					continue;
				}
			} else {
				if (tree_nodes[current].current.right) {
					current = tree_nodes[current].current.right;
					continue;
				}
			}
			return current;
		}
		if (tree_nodes[current].version == p_slice) {
			if (cross < 0 && tree_nodes[current].previous.left) {
				current = tree_nodes[current].previous.left;
				continue;
			}
			return tree_nodes[current].previous.prev;
		} else {
			if (cross < 0 && tree_nodes[current].current.left) {
				current = tree_nodes[current].current.left;
				continue;
			}
			return tree_nodes[current].current.prev;
		}
	}
}

int BentleyOttmann::edge_get_winding_previous(uint32_t p_treenode_edge, uint32_t p_version) {
	int winding = tree_nodes[p_treenode_edge].self_value;
	uint32_t current = p_treenode_edge;
	uint32_t parent;
	if (tree_nodes[p_treenode_edge].version == p_version) {
		parent = tree_nodes[p_treenode_edge].previous.parent;
		if (tree_nodes[tree_nodes[p_treenode_edge].previous.left].version == p_version) {
			winding += tree_nodes[tree_nodes[p_treenode_edge].previous.left].previous.sum_value;
		} else {
			winding += tree_nodes[tree_nodes[p_treenode_edge].previous.left].current.sum_value;
		}
	} else {
		parent = tree_nodes[p_treenode_edge].current.parent;
		if (tree_nodes[tree_nodes[p_treenode_edge].current.left].version == p_version) {
			winding += tree_nodes[tree_nodes[p_treenode_edge].current.left].previous.sum_value;
		} else {
			winding += tree_nodes[tree_nodes[p_treenode_edge].current.left].current.sum_value;
		}
	}
	while (parent) {
		if (tree_nodes[parent].version == p_version) {
			if (tree_nodes[parent].previous.right == current) {
				if (tree_nodes[tree_nodes[parent].previous.left].version == p_version) {
					winding += tree_nodes[tree_nodes[parent].previous.left].previous.sum_value + tree_nodes[parent].self_value;
				} else {
					winding += tree_nodes[tree_nodes[parent].previous.left].current.sum_value + tree_nodes[parent].self_value;
				}
			}
			current = parent;
			parent = tree_nodes[current].previous.parent;
		} else {
			if (tree_nodes[parent].current.right == current) {
				if (tree_nodes[tree_nodes[parent].current.left].version == p_version) {
					winding += tree_nodes[tree_nodes[parent].current.left].previous.sum_value + tree_nodes[parent].self_value;
				} else {
					winding += tree_nodes[tree_nodes[parent].current.left].current.sum_value + tree_nodes[parent].self_value;
				}
			}
			current = parent;
			parent = tree_nodes[current].current.parent;
		}
	}
	return winding;
}

void BentleyOttmann::check_intersection(uint32_t p_treenode_edge) {
	DEV_ASSERT(p_treenode_edge != edges_tree && tree_nodes[p_treenode_edge].current.next != edges_tree);
	Edge &edge1 = edges[tree_nodes[p_treenode_edge].element];
	Edge &edge2 = edges[tree_nodes[tree_nodes[p_treenode_edge].current.next].element];
	if (edge1.max_y < edge2.min_y || edge1.point_start == edge2.point_start) {
		return;
	}
	int64_t max;
	if (slices[edge1.next_check].x < slices[edge2.next_check].x) {
		max = slices[edge1.next_check].x;
	} else {
		max = slices[edge2.next_check].x;
	}
	if ((max * edge2.dir_y + edge2.cross) * edge1.dir_x >= (max * edge1.dir_y + edge1.cross) * edge2.dir_x) {
		return;
	}
	int64_t total = edge2.cross * edge1.dir_x - edge1.cross * edge2.dir_x;
	int64_t factor = edge1.dir_y * edge2.dir_x - edge2.dir_y * edge1.dir_x;
	int64_t x = total / factor;
	int64_t mod = total % factor;
	// The intersection must be rounded down, to ensure the edges are still
	// in the same y-order before they are swapped
	if (mod < 0) {
		mod += factor;
		x--;
	}
	edge1.next_check = add_slice(x);
	list_insert(edge1.listnode_check, slices[edge1.next_check].check_list);
}

uint32_t BentleyOttmann::tree_create(uint32_t p_element, int p_value) {
	TreeNode node;
	node.previous.prev = node.previous.next = node.current.prev = node.current.next = tree_nodes.size();
	node.element = p_element;
	node.self_value = p_value;
	tree_nodes.push_back(node);
	return node.current.next;
}

template <bool simple>
void BentleyOttmann::tree_clear(uint32_t p_tree, uint32_t p_version) {
	uint32_t iter = tree_nodes[p_tree].current.next;
	while (iter != p_tree) {
		uint32_t next = tree_nodes[iter].current.next;
		tree_version<simple>(iter, p_version);
		tree_nodes[iter].current.left = tree_nodes[iter].current.right = tree_nodes[iter].current.parent = 0;
		tree_nodes[iter].current.prev = tree_nodes[iter].current.next = iter;
		tree_nodes[iter].current.is_heavy = false;
		tree_nodes[iter].current.sum_value = 0;
		tree_nodes[iter].current.size = 0;
		iter = next;
	}
	tree_version<simple>(p_tree, p_version);
	tree_nodes[p_tree].current.left = tree_nodes[p_tree].current.right = tree_nodes[p_tree].current.parent = 0;
	tree_nodes[p_tree].current.prev = tree_nodes[p_tree].current.next = iter;
	tree_nodes[p_tree].current.is_heavy = false;
	tree_nodes[p_tree].current.sum_value = 0;
	tree_nodes[p_tree].current.size = 0;
}

template <bool simple>
void BentleyOttmann::tree_insert(uint32_t p_insert_item, uint32_t p_insert_after, uint32_t p_version) {
	DEV_ASSERT(p_insert_item != 0 && p_insert_after != 0);
	tree_version<simple>(p_insert_item, p_version);
	tree_version<simple>(p_insert_after, p_version);
	tree_version<simple>(tree_nodes[p_insert_after].current.next, p_version);
	if (tree_nodes[p_insert_after].current.right == 0) {
		tree_nodes[p_insert_after].current.right = p_insert_item;
		tree_nodes[p_insert_item].current.parent = p_insert_after;
	} else {
		DEV_ASSERT(tree_nodes[tree_nodes[p_insert_after].current.next].current.left == 0);
		tree_nodes[tree_nodes[p_insert_after].current.next].current.left = p_insert_item;
		tree_nodes[p_insert_item].current.parent = tree_nodes[p_insert_after].current.next;
	}
	tree_nodes[p_insert_item].current.prev = p_insert_after;
	tree_nodes[p_insert_item].current.next = tree_nodes[p_insert_after].current.next;
	tree_nodes[tree_nodes[p_insert_after].current.next].current.prev = p_insert_item;
	tree_nodes[p_insert_after].current.next = p_insert_item;
	DEV_ASSERT(tree_nodes[p_insert_item].current.sum_value == 0);
	uint32_t item = p_insert_item;
	if constexpr (!simple) {
		while (item) {
			tree_version<simple>(item, p_version);
			tree_nodes[item].current.sum_value += tree_nodes[p_insert_item].self_value;
			tree_nodes[item].current.size++;
			item = tree_nodes[item].current.parent;
		}
	}
	item = p_insert_item;
	uint32_t parent = tree_nodes[item].current.parent;
	while (tree_nodes[parent].current.parent) {
		uint32_t sibling = tree_nodes[parent].current.left;
		if (sibling == item) {
			sibling = tree_nodes[parent].current.right;
		}
		if (tree_nodes[sibling].current.is_heavy) {
			tree_version<simple>(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = false;
			return;
		}
		if (!tree_nodes[item].current.is_heavy) {
			tree_version<simple>(item, p_version);
			tree_nodes[item].current.is_heavy = true;
			item = parent;
			parent = tree_nodes[item].current.parent;
			continue;
		}
		uint32_t move;
		uint32_t unmove;
		uint32_t move_move;
		uint32_t move_unmove;
		if (item == tree_nodes[parent].current.left) {
			move = tree_nodes[item].current.right;
			unmove = tree_nodes[item].current.left;
			move_move = tree_nodes[move].current.left;
			move_unmove = tree_nodes[move].current.right;
		} else {
			move = tree_nodes[item].current.left;
			unmove = tree_nodes[item].current.right;
			move_move = tree_nodes[move].current.right;
			move_unmove = tree_nodes[move].current.left;
		}
		if (!tree_nodes[move].current.is_heavy) {
			tree_version<simple>(parent, p_version);
			tree_rotate<simple>(item, p_version);
			tree_nodes[item].current.is_heavy = tree_nodes[parent].current.is_heavy;
			tree_nodes[parent].current.is_heavy = !tree_nodes[unmove].current.is_heavy;
			if (tree_nodes[unmove].current.is_heavy) {
				tree_version<simple>(unmove, p_version);
				tree_nodes[unmove].current.is_heavy = false;
				return;
			}
			DEV_ASSERT(move != 0);
			tree_version<simple>(move, p_version);
			tree_nodes[move].current.is_heavy = true;
			parent = tree_nodes[item].current.parent;
			continue;
		}
		tree_rotate<simple>(move, p_version);
		tree_rotate<simple>(move, p_version);
		tree_nodes[move].current.is_heavy = tree_nodes[parent].current.is_heavy;
		if (unmove != 0) {
			tree_version<simple>(unmove, p_version);
			tree_nodes[unmove].current.is_heavy = tree_nodes[move_unmove].current.is_heavy;
		}
		if (sibling != 0) {
			tree_version<simple>(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = tree_nodes[move_move].current.is_heavy;
		}
		tree_nodes[item].current.is_heavy = false;
		tree_nodes[parent].current.is_heavy = false;
		tree_nodes[move_move].current.is_heavy = false;
		if (move_unmove != 0) {
			tree_version<simple>(move_unmove, p_version);
			tree_nodes[move_unmove].current.is_heavy = false;
		}
		return;
	}
}

template <bool simple>
void BentleyOttmann::tree_remove(uint32_t p_remove_item, uint32_t p_version) {
	DEV_ASSERT(tree_nodes[p_remove_item].current.parent != 0);
	if (tree_nodes[p_remove_item].current.left != 0 && tree_nodes[p_remove_item].current.right != 0) {
		uint32_t prev = tree_nodes[p_remove_item].current.prev;
		DEV_ASSERT(tree_nodes[prev].current.parent != 0 && tree_nodes[prev].current.right == 0);
		tree_swap<simple>(p_remove_item, prev, p_version);
	}
	DEV_ASSERT(tree_nodes[p_remove_item].current.left == 0 || tree_nodes[p_remove_item].current.right == 0);
	uint32_t prev = tree_nodes[p_remove_item].current.prev;
	uint32_t next = tree_nodes[p_remove_item].current.next;
	tree_version<simple>(prev, p_version);
	tree_version<simple>(next, p_version);
	tree_nodes[prev].current.next = next;
	tree_nodes[next].current.prev = prev;
	uint32_t parent = tree_nodes[p_remove_item].current.parent;
	uint32_t replacement = tree_nodes[p_remove_item].current.left;
	if (replacement == 0) {
		replacement = tree_nodes[p_remove_item].current.right;
	}
	if (replacement != 0) {
		tree_version<simple>(replacement, p_version);
		tree_nodes[replacement].current.parent = parent;
		tree_nodes[replacement].current.is_heavy = tree_nodes[p_remove_item].current.is_heavy;
	}
	tree_version<simple>(parent, p_version);
	if (tree_nodes[parent].current.left == p_remove_item) {
		tree_nodes[parent].current.left = replacement;
	} else {
		tree_nodes[parent].current.right = replacement;
	}
	tree_version<simple>(p_remove_item, p_version);
	tree_nodes[p_remove_item].current.left = tree_nodes[p_remove_item].current.right = tree_nodes[p_remove_item].current.parent = 0;
	tree_nodes[p_remove_item].current.prev = tree_nodes[p_remove_item].current.next = p_remove_item;
	tree_nodes[p_remove_item].current.is_heavy = false;
	uint32_t item = parent;
	if constexpr (!simple) {
		tree_nodes[p_remove_item].current.sum_value = 0;
		tree_nodes[p_remove_item].current.size = 0;
		while (item) {
			tree_version<simple>(item, p_version);
			tree_nodes[item].current.sum_value -= tree_nodes[p_remove_item].self_value;
			tree_nodes[item].current.size--;
			item = tree_nodes[item].current.parent;
		}
	}
	item = replacement;
	if (tree_nodes[parent].current.left == 0 && tree_nodes[parent].current.right == 0) {
		item = parent;
		parent = tree_nodes[item].current.parent;
	}
	while (tree_nodes[parent].current.parent != 0) {
		uint32_t sibling = tree_nodes[parent].current.left;
		if (sibling == item) {
			sibling = tree_nodes[parent].current.right;
		}
		DEV_ASSERT(sibling != 0);
		if (tree_nodes[item].current.is_heavy) {
			tree_version<simple>(item, p_version);
			tree_nodes[item].current.is_heavy = false;
			item = parent;
			parent = tree_nodes[item].current.parent;
			continue;
		}
		if (!tree_nodes[sibling].current.is_heavy) {
			tree_version<simple>(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = true;
			return;
		}
		uint32_t move;
		uint32_t unmove;
		uint32_t move_move;
		uint32_t move_unmove;
		if (sibling == tree_nodes[parent].current.left) {
			move = tree_nodes[sibling].current.right;
			unmove = tree_nodes[sibling].current.left;
			move_move = tree_nodes[move].current.left;
			move_unmove = tree_nodes[move].current.right;
		} else {
			move = tree_nodes[sibling].current.left;
			unmove = tree_nodes[sibling].current.right;
			move_move = tree_nodes[move].current.right;
			move_unmove = tree_nodes[move].current.left;
		}
		if (!tree_nodes[move].current.is_heavy) {
			tree_version<simple>(parent, p_version);
			tree_rotate<simple>(sibling, p_version);
			tree_nodes[sibling].current.is_heavy = tree_nodes[parent].current.is_heavy;
			tree_nodes[parent].current.is_heavy = !tree_nodes[unmove].current.is_heavy;
			if (tree_nodes[unmove].current.is_heavy) {
				tree_version<simple>(unmove, p_version);
				tree_nodes[unmove].current.is_heavy = false;
				item = sibling;
				parent = tree_nodes[item].current.parent;
				continue;
			}
			DEV_ASSERT(move != 0);
			tree_version<simple>(move, p_version);
			tree_nodes[move].current.is_heavy = true;
			return;
		}
		tree_rotate<simple>(move, p_version);
		tree_rotate<simple>(move, p_version);
		tree_nodes[move].current.is_heavy = tree_nodes[parent].current.is_heavy;
		if (unmove != 0) {
			tree_version<simple>(unmove, p_version);
			tree_nodes[unmove].current.is_heavy = tree_nodes[move_unmove].current.is_heavy;
		}
		if (item != 0) {
			tree_version<simple>(item, p_version);
			tree_nodes[item].current.is_heavy = tree_nodes[move_move].current.is_heavy;
		}
		tree_nodes[sibling].current.is_heavy = false;
		tree_nodes[parent].current.is_heavy = false;
		tree_nodes[move_move].current.is_heavy = false;
		if (move_unmove != 0) {
			tree_version<simple>(move_unmove, p_version);
			tree_nodes[move_unmove].current.is_heavy = false;
		}
		item = move;
		parent = tree_nodes[item].current.parent;
		continue;
	}
}

template <bool simple>
void BentleyOttmann::tree_rotate(uint32_t p_item, uint32_t p_version) {
	DEV_ASSERT(tree_nodes[tree_nodes[p_item].current.parent].current.parent != 0);
	uint32_t parent = tree_nodes[p_item].current.parent;
	tree_version<simple>(p_item, p_version);
	tree_version<simple>(parent, p_version);
	if (tree_nodes[parent].current.left == p_item) {
		uint32_t move = tree_nodes[p_item].current.right;
		tree_nodes[parent].current.left = move;
		tree_nodes[p_item].current.right = parent;
		if (move) {
			tree_version<simple>(move, p_version);
			tree_nodes[move].current.parent = parent;
		}
	} else {
		uint32_t move = tree_nodes[p_item].current.left;
		tree_nodes[parent].current.right = move;
		tree_nodes[p_item].current.left = parent;
		if (move) {
			tree_version<simple>(move, p_version);
			tree_nodes[move].current.parent = parent;
		}
	}
	uint32_t grandparent = tree_nodes[parent].current.parent;
	tree_version<simple>(grandparent, p_version);
	tree_nodes[p_item].current.parent = grandparent;
	if (tree_nodes[grandparent].current.left == parent) {
		tree_nodes[grandparent].current.left = p_item;
	} else {
		tree_nodes[grandparent].current.right = p_item;
	}
	tree_nodes[parent].current.parent = p_item;
	if constexpr (!simple) {
		tree_nodes[parent].current.sum_value = tree_nodes[parent].self_value + tree_nodes[tree_nodes[parent].current.left].current.sum_value + tree_nodes[tree_nodes[parent].current.right].current.sum_value;
		tree_nodes[p_item].current.sum_value = tree_nodes[p_item].self_value + tree_nodes[tree_nodes[p_item].current.left].current.sum_value + tree_nodes[tree_nodes[p_item].current.right].current.sum_value;
		tree_nodes[parent].current.size = tree_nodes[tree_nodes[parent].current.left].current.size + tree_nodes[tree_nodes[parent].current.right].current.size + 1;
		tree_nodes[p_item].current.size = tree_nodes[tree_nodes[p_item].current.left].current.size + tree_nodes[tree_nodes[p_item].current.right].current.size + 1;
	}
}

template <bool simple>
void BentleyOttmann::tree_swap(uint32_t p_item1, uint32_t p_item2, uint32_t p_version) {
	DEV_ASSERT(tree_nodes[p_item1].current.parent != 0 && tree_nodes[p_item2].current.parent != 0);
	tree_version<simple>(p_item1, p_version);
	tree_version<simple>(p_item2, p_version);
	uint32_t parent1 = tree_nodes[p_item1].current.parent;
	uint32_t left1 = tree_nodes[p_item1].current.left;
	uint32_t right1 = tree_nodes[p_item1].current.right;
	uint32_t prev1 = tree_nodes[p_item1].current.prev;
	uint32_t next1 = tree_nodes[p_item1].current.next;
	uint32_t parent2 = tree_nodes[p_item2].current.parent;
	uint32_t left2 = tree_nodes[p_item2].current.left;
	uint32_t right2 = tree_nodes[p_item2].current.right;
	uint32_t prev2 = tree_nodes[p_item2].current.prev;
	uint32_t next2 = tree_nodes[p_item2].current.next;
	tree_version<simple>(parent1, p_version);
	tree_version<simple>(prev1, p_version);
	tree_version<simple>(next1, p_version);
	tree_version<simple>(parent2, p_version);
	tree_version<simple>(prev2, p_version);
	tree_version<simple>(next2, p_version);
	if (tree_nodes[parent1].current.left == p_item1) {
		tree_nodes[parent1].current.left = p_item2;
	} else {
		tree_nodes[parent1].current.right = p_item2;
	}
	if (tree_nodes[parent2].current.left == p_item2) {
		tree_nodes[parent2].current.left = p_item1;
	} else {
		tree_nodes[parent2].current.right = p_item1;
	}
	if (left1) {
		tree_version<simple>(left1, p_version);
		tree_nodes[left1].current.parent = p_item2;
	}
	if (right1) {
		tree_version<simple>(right1, p_version);
		tree_nodes[right1].current.parent = p_item2;
	}
	if (left2) {
		tree_version<simple>(left2, p_version);
		tree_nodes[left2].current.parent = p_item1;
	}
	if (right2) {
		tree_version<simple>(right2, p_version);
		tree_nodes[right2].current.parent = p_item1;
	}
	tree_nodes[prev1].current.next = p_item2;
	tree_nodes[next1].current.prev = p_item2;
	tree_nodes[prev2].current.next = p_item1;
	tree_nodes[next2].current.prev = p_item1;
	parent1 = tree_nodes[p_item1].current.parent;
	left1 = tree_nodes[p_item1].current.left;
	right1 = tree_nodes[p_item1].current.right;
	prev1 = tree_nodes[p_item1].current.prev;
	next1 = tree_nodes[p_item1].current.next;
	parent2 = tree_nodes[p_item2].current.parent;
	left2 = tree_nodes[p_item2].current.left;
	right2 = tree_nodes[p_item2].current.right;
	prev2 = tree_nodes[p_item2].current.prev;
	next2 = tree_nodes[p_item2].current.next;
	tree_nodes[p_item2].current.parent = parent1;
	tree_nodes[p_item2].current.left = left1;
	tree_nodes[p_item2].current.right = right1;
	tree_nodes[p_item2].current.prev = prev1;
	tree_nodes[p_item2].current.next = next1;
	tree_nodes[p_item1].current.parent = parent2;
	tree_nodes[p_item1].current.left = left2;
	tree_nodes[p_item1].current.right = right2;
	tree_nodes[p_item1].current.prev = prev2;
	tree_nodes[p_item1].current.next = next2;
	bool is_heavy = tree_nodes[p_item1].current.is_heavy;
	tree_nodes[p_item1].current.is_heavy = tree_nodes[p_item2].current.is_heavy;
	tree_nodes[p_item2].current.is_heavy = is_heavy;
	if constexpr (!simple) {
		int sum_value = tree_nodes[p_item1].current.sum_value;
		tree_nodes[p_item1].current.sum_value = tree_nodes[p_item2].current.sum_value;
		tree_nodes[p_item2].current.sum_value = sum_value;
		uint32_t size = tree_nodes[p_item1].current.size;
		tree_nodes[p_item1].current.size = tree_nodes[p_item2].current.size;
		tree_nodes[p_item2].current.size = size;
		int diff = tree_nodes[p_item1].self_value - tree_nodes[p_item2].self_value;
		if (diff) {
			while (p_item1) {
				tree_version<simple>(p_item1, p_version);
				tree_nodes[p_item1].current.sum_value += diff;
				p_item1 = tree_nodes[p_item1].current.parent;
			}
			while (p_item2) {
				tree_version<simple>(p_item2, p_version);
				tree_nodes[p_item2].current.sum_value -= diff;
				p_item2 = tree_nodes[p_item2].current.parent;
			}
		}
	}
}

template <>
void BentleyOttmann::tree_version<false>(uint32_t p_item, uint32_t p_version) {
	DEV_ASSERT(p_item != 0);
	if (tree_nodes[p_item].version == p_version) {
		return;
	}
	tree_nodes[p_item].version = p_version;
	tree_nodes[p_item].previous = tree_nodes[p_item].current;
}

template <>
void BentleyOttmann::tree_version<true>(uint32_t p_item, uint32_t p_version) {
}

void BentleyOttmann::tree_index(uint32_t p_item) {
	int index = tree_nodes[tree_nodes[p_item].current.left].current.size;
	uint32_t current = p_item;
	uint32_t parent = tree_nodes[current].current.parent;
	while (parent) {
		if (tree_nodes[parent].current.right == current) {
			index += tree_nodes[tree_nodes[parent].current.left].current.size + 1;
		}
		current = parent;
		parent = tree_nodes[current].current.parent;
	}
	tree_nodes[p_item].current.index = index;
}

void BentleyOttmann::tree_index_previous(uint32_t p_item, uint32_t p_version) {
	int index;
	uint32_t current = p_item;
	uint32_t parent;
	if (tree_nodes[p_item].version == p_version) {
		parent = tree_nodes[p_item].previous.parent;
		if (tree_nodes[tree_nodes[p_item].previous.left].version == p_version) {
			index = tree_nodes[tree_nodes[p_item].previous.left].previous.size;
		} else {
			index = tree_nodes[tree_nodes[p_item].previous.left].current.size;
		}
	} else {
		parent = tree_nodes[p_item].current.parent;
		if (tree_nodes[tree_nodes[p_item].current.left].version == p_version) {
			index = tree_nodes[tree_nodes[p_item].current.left].previous.size;
		} else {
			index = tree_nodes[tree_nodes[p_item].current.left].current.size;
		}
	}
	while (parent) {
		if (tree_nodes[parent].version == p_version) {
			if (tree_nodes[parent].previous.right == current) {
				if (tree_nodes[tree_nodes[parent].previous.left].version == p_version) {
					index += tree_nodes[tree_nodes[parent].previous.left].previous.size + 1;
				} else {
					index += tree_nodes[tree_nodes[parent].previous.left].current.size + 1;
				}
			}
			current = parent;
			parent = tree_nodes[current].previous.parent;
		} else {
			if (tree_nodes[parent].current.right == current) {
				if (tree_nodes[tree_nodes[parent].current.left].version == p_version) {
					index += tree_nodes[tree_nodes[parent].current.left].previous.size + 1;
				} else {
					index += tree_nodes[tree_nodes[parent].current.left].current.size + 1;
				}
			}
			current = parent;
			parent = tree_nodes[current].current.parent;
		}
	}
	tree_nodes[p_item].previous.index = index;
}

uint32_t BentleyOttmann::list_create(uint32_t p_element) {
	ListNode node;
	node.anchor = node.prev = node.next = list_nodes.size();
	node.element = p_element;
	list_nodes.push_back(node);
	return node.next;
}

void BentleyOttmann::list_insert(uint32_t p_insert_item, uint32_t p_list) {
	DEV_ASSERT(p_insert_item != p_list);
	DEV_ASSERT(list_nodes[p_list].anchor == p_list);
	if (list_nodes[p_insert_item].anchor == p_list) {
		return;
	}
	if (list_nodes[p_insert_item].anchor != p_insert_item) {
		list_remove(p_insert_item);
	}
	list_nodes[p_insert_item].anchor = p_list;
	list_nodes[p_insert_item].prev = p_list;
	list_nodes[p_insert_item].next = list_nodes[p_list].next;
	list_nodes[list_nodes[p_list].next].prev = p_insert_item;
	list_nodes[p_list].next = p_insert_item;
}

void BentleyOttmann::list_remove(uint32_t p_remove_item) {
	list_nodes[list_nodes[p_remove_item].next].prev = list_nodes[p_remove_item].prev;
	list_nodes[list_nodes[p_remove_item].prev].next = list_nodes[p_remove_item].next;
	list_nodes[p_remove_item].anchor = list_nodes[p_remove_item].prev = list_nodes[p_remove_item].next = p_remove_item;
}
