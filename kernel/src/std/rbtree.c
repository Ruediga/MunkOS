#include "rbtree.h"
#include "interrupt.h"
#include "kprintf.h"
#include "kheap.h"

rb_tree_node_t sentinel = {
    NULL,
    { RB_NIL, RB_NIL },
    RB_BLACK,
    0
};

static inline rb_tree_node_t *_new_node(struct rb_tree_data *data, rb_tree_node_t *parent)
{
    rb_tree_node_t *node = kmalloc(sizeof(rb_tree_node_t));
    node->child[RB_LEFT] = RB_NIL;
    node->child[RB_RIGHT] = RB_NIL;
    node->color = RB_RED;
    node->data = data;
    node->parent = parent;
    return node;
}

static void _rotate(rb_tree_node_t **root, rb_tree_node_t *subtree, enum rb_tree_direction dir)
{
    // rotate subtree left or right:
    // new_subtree_root = subtree.child[dir]
    // subtree = new_subtree_root
    // subtree.child[dir] = old_subtree

    rb_tree_node_t *n = subtree->child[1 - dir];

    subtree->child[1 - dir] = n->child[dir];
    if (n->child[dir] != RB_NIL)
        n->child[dir]->parent = subtree;

    if (n != RB_NIL)
        n->parent = subtree->parent;

    if (subtree->parent) {
        enum rb_tree_direction lr = subtree == subtree->parent->child[dir] ?
            dir : 1 - dir;
        subtree->parent->child[lr] = n;
    }
    else
        *root = n;

    n->child[dir] = subtree;
    if (subtree != RB_NIL)
        subtree->parent = n;
}

static void _insert_fixup(rb_tree_node_t **root, rb_tree_node_t *node)
{
    // keep properties preserved:
    // - every node is either red or black
    // - all leave nodes are considered black
    // - a red node has a black child
    // - every path from a given node to any of it's leaves goes
    //   through the same number of black nodes

    while (node != *root && node->parent->color == RB_RED) {
        // will be right if left and left if right
        enum rb_tree_direction dir = node->parent == node->parent->parent->child[RB_LEFT];
        rb_tree_node_t *n = node->parent->parent->child[dir];

        if (n->color == RB_RED) {
            node->parent->color = RB_BLACK;
            n->color = RB_BLACK;
            node->parent->parent->color = RB_RED;
            node = node->parent->parent;
        } else {
            if (node == node->parent->child[dir]) {
                node = node->parent;
                _rotate(root, node, 1 - dir);
            }

            node->parent->color = RB_BLACK;
            node->parent->parent->color = RB_RED;
            _rotate(root, node->parent->parent, dir);
        }
    }

    (*root)->color = RB_BLACK;
}

// return NULL on success, rb_tree_data * to duplicate if key already exists
struct rb_tree_data *tree_insert(rb_tree_node_t **root, struct rb_tree_data *data)
{
    kprintf("\033[33minserting %lu\033[0m\n", data->key);
    rb_tree_node_t *curr, *parent;

    // find pos
    curr = *root;
    parent = NULL;
    while (curr != RB_NIL) {
        // By definition, a binary tree may not contain duplicates since every key of any internal
        // node has to be greater than all the keys in the respective node's left subtree and
        // less than the ones in its right subtree. Violating this rule may make the tree
        // unbalanced, which in terms of a red-black-tree would mean that not every path from a
        // given node to any of it's leaves would go throught the same number of black nodes.
        // FIND PROOF FOR THIS ^ !
        // when inserting, we return the key where a duplicate has been found, so we don't have
        // to search for the node beforehand and we can easily keep an external bucket.
        if (curr->data->key == data->key)
            return curr->data;
        parent = curr;
        curr = curr->child[data->key < curr->data->key];
    }

    rb_tree_node_t *head = _new_node(data, parent);
    head->parent = parent;

    // insert
    if (parent)
        parent->child[head->data->key < parent->data->key] = head;
    else
        *root = head;

    _insert_fixup(root, head);

    return NULL;
}

static void _remove_fixup(rb_tree_node_t **root, rb_tree_node_t *node)
{
    // keep properties preserved:
    // - every node is either red or black
    // - all leave nodes are considered black
    // - a red node has a black child
    // - every path from a given node to any of it's leaves goes
    //   through the same number of black nodes

    while (node != *root && node->color == RB_BLACK) {
        enum rb_tree_direction dir = node == node->parent->child[RB_LEFT];
        // will be right if left and left if right
        rb_tree_node_t *sibling = node->parent->child[dir];

        if (sibling->color == RB_RED) {
            sibling->color = RB_BLACK;
            node->parent->color = RB_RED;
            _rotate(root, node->parent, 1 - dir);
            sibling = node->parent->child[dir];
        }

        if (sibling->child[1 - dir]->color == RB_BLACK && sibling->child[dir]->color == RB_BLACK) {
            sibling->color = RB_RED;
            node = node->parent;
        } else {
            if (sibling->child[dir]->color == RB_BLACK) {
                sibling->child[1 - dir]->color = RB_BLACK;
                sibling->color = RB_RED;
                _rotate(root, sibling, dir);
                sibling = node->parent->child[dir];
            }
            sibling->color = node->parent->color;
            node->parent->color = RB_BLACK;
            sibling->child[dir]->color = RB_BLACK;
            _rotate(root, node->parent, 1 - dir);
            node = *root;
        }
    }

    node->color = RB_BLACK;
}

static rb_tree_node_t *_find_node(rb_tree_node_t *root, size_t key)
{
    while (root != RB_NIL) {
        if (root->data->key == key)
            return root;
        else
            root = root->child[key < root->data->key];
    }
    return NULL;
}

// return NULL if key didn't exist, and a pointer to the node that has been removed
struct rb_tree_data *tree_remove(rb_tree_node_t **root, size_t key)
{
    kprintf("\033[31mremoving %lu\033[0m\n", key);
    rb_tree_node_t *n = _find_node(*root, key);
    rb_tree_node_t *node, *x;

    if (!n || n == RB_NIL)
        // key not found
        return NULL;
    // key found
    struct rb_tree_data *out = n->data;

    if (n->child[RB_LEFT] == RB_NIL || n->child[RB_RIGHT] == RB_NIL)
        x = n;
    else {
        x = n->child[RB_RIGHT];
        while (x->child[RB_LEFT] != RB_NIL)
            x = x->child[RB_LEFT];
    }

    node = x->child[x->child[RB_LEFT] == RB_NIL];

    node->parent = x->parent;
    if (x->parent)
        x->parent->child[x != x->parent->child[RB_LEFT]] = node;
    else
        *root = node;

    if (x != n)
        n->data = x->data;

    if (x->color == RB_BLACK)
        _remove_fixup(root, node);

    kfree(x);
    return out;
}

// if multiple occurences of the same key, always returns the first one, if not present, return NULL
struct rb_tree_data *tree_find(rb_tree_node_t **root, size_t key)
{
    rb_tree_node_t *ret = _find_node(*root, key);
    return ret ? ret->data : NULL;
}

void tree_set_data_at(rb_tree_node_t **root, size_t key, struct rb_tree_data *data)
{
    rb_tree_node_t *found = _find_node(*root, key);
    if (key == data->key)
        found->data = data;
    else
        kpanic(0, NULL, "wrong key");
}

void tree_debug_print(rb_tree_node_t *root, size_t depth)
{   
    if (root != RB_NIL) {
        tree_debug_print(root->child[RB_LEFT], depth + 1);
        for (size_t i = 0; i < depth; i++)
            kprintf("    ");
        kprintf("Key at %p: [%lu] - Color: %s - Parent: %p, depth=%lu\n",
               root, root->data->key, (root->color == RB_RED) ?
               "RB_RED" : "RB_BLACK", root->parent, depth);
        tree_debug_print(root->child[RB_RIGHT], depth + 1);
    }
}