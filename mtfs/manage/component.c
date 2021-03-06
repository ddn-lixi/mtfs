/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include "fsmL_internal.h"
#include "component_internal.h"
#include "workspace_internal.h"
#include <debug.h>
#include <memory.h>
#include <log.h>

static struct component component_types;
int component_types_init()
{
	int ret = 0;
	MENTRY();

	MTFS_STRDUP(component_types.name, "types");
	component_types.is_graph = 1;
	component_types.mode = COMPONENT_READ_MODE;
	component_types.ref = 1;

	MTFS_INIT_LIST_HEAD(&component_types.this_graph.subcomponents);
	ret = component_insert(&component_root, &component_types);
	if (ret) {
		MERROR("failed to insert component types\n");
		goto out_free_name;
	}
	goto out;
out_free_name:
	MTFS_FREE_STR(component_types.name);
out:
	MRETURN(ret);
}

struct component *component_search_type(const char *type_name)
{
	return component_search(&component_types, type_name);
}

static int component_register_type(struct component *component, const char *type_name, int need_copy)
{
	int ret = 0;
	MENTRY();

	MASSERT(component);
	MASSERT(type_name);
	MASSERT(component->type == NULL);
	MASSERT(component->pins);

	if (need_copy) {
		struct component *found = NULL;
		struct component *new_component = NULL;
		
		found = component_search_type(type_name);
		if (found != NULL) {
			if (found != component) {
				MERROR("try to register type %s for multiple times\n",
				       type_name);
			} else {
				MERROR("%s has already been registered\n",
				       component->name);
			}
			ret = -EALREADY;
			goto out;
		}
	
		new_component = component_copy(component, &component_types, type_name);
		if (IS_ERR(new_component)) {
			ret = PTR_ERR(new_component);
			goto out;
		}
		new_component->type = new_component;
	} else {
		ret = component_insert(&component_types, component);
		component->type = component;
	}
out:
	MRETURN(ret);
}

int component_unregister_type(struct component *type)
{
	int ret = 0;
	MENTRY();

	if (type->ref != 1) {
		MERROR("%s is being used by some instances\n",
		       type->name);
		ret = -EBUSY;
		goto out;
	}
	component_type_put(type);
out:
	MRETURN(ret);
}

struct component *component_get(struct component *parent, const char *type_name)
{
	struct component *component = NULL;
	MENTRY();

	component = component_search(parent, type_name);
	if (component) {
		component->ref ++;
	}

	MRETURN(component);
}

void component_put(struct component *component)
{
	MENTRY();
	MASSERT(component->ref > 0);
	component->ref --;

	if (component->ref == 0) {
		component_remove(component);
	}
	_MRETURN();
}

struct component *component_type_get(const char *type_name)
{
	return component_get(&component_types, type_name);
}

void component_type_put(struct component *type)
{
	MENTRY();
	component_put(type);
	_MRETURN();
}

int component_dump(struct component *component)
{
	int i = 0;
	MENTRY();

	MASSERT(component);
	MPRINT("%s: ", component->name);
	if (component->type == NULL) {
		MPRINT("type=NULL, ");
	} else {
		MPRINT("type=\"%s\", ", component->type->name);
	}

	if (component->pins) {
		MPRINT("pins={");
		for(i = 0; i < component->pin_num - 1; i++) {
			MPRINT("\"%s\", ", component->pins[i].name);
		}
		MPRINT("\"%s\"}, ", component->pins[component->pin_num - 1].name);
	} else {
		MPRINT("pins=NULL, ");
	}
	MPRINT("%s\n", component->is_graph ? "is_graph" : "is_filter");

	MRETURN(0);
}

static struct pin *component_alloc_pins(int pin_num)
{
	struct pin *pins = NULL;
	MENTRY();

	MTFS_ALLOC(pins, sizeof(*pins) * pin_num);
	if (pins == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}
out:
	MRETURN(pins);
}

struct component *component_alloc(int pin_num)
{
	struct component *component = NULL;
	MENTRY();

	MTFS_ALLOC_PTR(component);
	if (component == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}

	component->pin_num = pin_num;
	component->pins = component_alloc_pins(pin_num);
	if (component->pins == NULL) {
		MERROR("not enough memory\n");
		goto out_free_instance;
	}

	goto out;
	MTFS_FREE(component->pins, sizeof(*component->pins) * component->pin_num);
out_free_instance:
	MTFS_FREE_PTR(component);
	component = NULL;
out:
	MRETURN(component);
}

void component_free(struct component *component)
{
	int i = 0;
	MENTRY();

	if (component->name) {
		MTFS_FREE_STR(component->name);
	}

	if (component->pins) {
		for (i = 0; i < component->pin_num; i++) {
			if (component->pins[i].name) {
				MTFS_FREE_STR(component->pins[i].name);
			}
		}
		MTFS_FREE(component->pins, sizeof(*component->pins) * component->pin_num);
	}

	MTFS_FREE_PTR(component);
	_MRETURN();
}

struct component *component_instance_new(struct component *type, const char *instance_name)
{
	struct component *instance = NULL;
	int i = 0;
	int ret = 0;
	MENTRY();

	instance = component_alloc(type->pin_num);
	if (instance == NULL) {
		ret = -ENOMEM;
		MERROR("not enough memory\n");
		goto out;
	}
	instance->ref = 1;

	/* Init instance fields */
	MTFS_STRDUP(instance->name, instance_name);
	if (instance->name == NULL) {
		ret = -ENOMEM;
		goto out_free_component;
	}

	/* Init common fields */
	for(i = 0; i < instance->pin_num; i++) {
		MTFS_STRDUP(instance->pins[i].name, type->pins[i].name);
		if (instance->pins[i].name == NULL) {
			ret = -ENOMEM;
			goto out_free_component;
		}
		instance->pins[i].is_input = type->pins[i].is_input;
		instance->pins[i].parent = instance;
		MTFS_INIT_LIST_HEAD(&instance->pins[i].prevs);
	}

	instance->is_graph = type->is_graph;
	/* TODO: copy this_graph */
	if (instance->is_graph) {
		MTFS_INIT_LIST_HEAD(&instance->this_graph.subcomponents);
		ret = component_copy_subs(type, instance);
		if (ret) {
			goto out_remove_subs;
		}
	}

	instance->type = type;

	goto out;
out_remove_subs:
	component_subs_eachdo(instance, component_remove);
out_free_component:
	component_free(instance);
out:
	if (IS_ERR(ret)) {
		instance = ERR_PTR(ret);
	}
	MRETURN(instance);
}

int component_rename(struct component *component, const char *name)
{
	char *old_name = component->name;
	int ret = 0;
	MENTRY();

	MTFS_STRDUP(component->name, name);
	if (component->name) {
		MTFS_FREE_STR(old_name);
	} else {
		component->name = old_name;
		ret = -ENOMEM;
	}	

	MRETURN(ret);
}

struct component *component_new_notype(struct component *parent, const char *name)
{
	struct component *component = NULL;
	struct graph *graph = NULL;
	int ret = 0;
	MENTRY();

	MTFS_ALLOC_PTR(component);
	if (component == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	component->ref = 1;
	graph = &component->this_graph;

	MTFS_STRDUP(component->name, name);
	if (component->name == NULL) {
		ret = -ENOMEM;
		goto out_free_component;
	}
	component->is_graph = 1;

	MTFS_INIT_LIST_HEAD(&graph->subcomponents);
	ret = component_insert(parent, component);
	if (ret) {
		goto out_free_name;
	}
	component->parent = parent;
	goto out;
out_free_name:
	MTFS_FREE_STR(component->name);
out_free_component:
	MTFS_FREE_PTR(component);
out:
	if (ret) {
		component = ERR_PTR(ret);
	}
	MRETURN(component);
}

struct component *component_new(struct component *parent, const char *type_name, const char *instance_name)
{
	int ret = 0;
	struct component *type = NULL;
	struct component *instance = NULL;
	MENTRY();

	if(type_name == NULL || instance_name == NULL) {
		ret = -EINVAL;
		goto out;
	}

	type = component_type_get(type_name);
	if (type == NULL) {
		MERROR("type %s not exist\n", type_name);
		ret = -EINVAL; /* Which errno? */
		goto out;
	}

	instance = component_instance_new(type, instance_name);
	if (IS_ERR(instance)) {
		MERROR("failed to get instance for type %s\n", type_name);
		ret = PTR_ERR(instance); /* Which errno? */
		goto out_put_type;
	}

	ret = component_insert(parent, instance);
	if (ret) {
		goto out_free_instance;
	}
	instance->parent = parent;
	goto out;
out_free_instance:
	component_free(instance);
	//_graph_rm_graph(type->this_graph, instance->this_graph);
out_put_type:
	component_type_put(type);
out:
	if (ret) {
		instance = ERR_PTR(ret);
	}
	MRETURN(instance);
}

static int component_list_delete(struct component *component)
{
	struct mtfs_list_head *p = NULL;
	struct component *parent = component->parent;
	MENTRY();

	mtfs_list_for_each(p, &parent->this_graph.subcomponents) {
		struct component *found;

		found = list_entry(p, typeof(*component), component_list);
		if (found == component) {
			mtfs_list_del(p);
			break;
		}
	}

	MRETURN(0);
}

int component_declare_graph(struct component *component, const char *type_name)
{
	int ret = 0;
	MENTRY();

	ret = component_register_type(component, type_name, 1);

	MRETURN(ret);
}

int component_declare_filter(const char *type_name, const char *inpin_name, int out_num, const char **outpin_names, int selector)
{
	int ret = 0;
	struct component *type = NULL;
	int i = 0;
	MENTRY();

	MASSERT(type_name);
	MASSERT(out_num > 0);

	type = component_alloc(1 + out_num);
	if (type == NULL) {
		MERROR("not enough memory\n");
		ret = -ENOMEM;
		goto out;
	}

	MTFS_STRDUP(type->pins[0].name, inpin_name);
	if (type->pins[0].name == NULL) {
		goto out_free_type;
	}
	type->pins[0].is_input = 1;

	for(i = 1; i < type->pin_num; i++) {
		MTFS_STRDUP(type->pins[i].name, outpin_names[i - 1]);
		if (type->pins[i].name == NULL) {
			goto out_free_type;
		}
		type->pins[i].is_input = 0;
	}

	MTFS_STRDUP(type->name, type_name);
	if (type->name == NULL) {
		goto out_free_type;
	}

	ret = component_register_type(type, type_name, 0);
	if (ret) {
		MERROR("failed to register type: %s\n", type_name);
		goto out_free_type;
	}
	goto out;
out_free_type:
	component_free(type);
out:
	MRETURN(ret);
}

int component_types_eachdo(int (*func)(struct component *subcomponent))
{
	return component_subs_eachdo(&component_types, func);
}

static int componen_output_pin_unlink(struct pin *output_pin)
{
	int ret = 0;
	MENTRY();

	MASSERT(!output_pin->is_input);
	if (output_pin->next == NULL) {
		goto out;
	}

	mtfs_list_del(&(output_pin->pin_list));
	output_pin->next = NULL;

out:
	MRETURN(ret);
}

/* 
 * @func can unlink
 */
int componen_input_pin_unlink(struct pin *input_pin)
{
	struct component *prev_component = NULL;
	struct pin *prev_pin = NULL;
	struct mtfs_list_head *p = NULL;
	struct mtfs_list_head *tmp = NULL;
	int ret = 0;
	MENTRY();

	MASSERT(input_pin->is_input);

	MDEBUG("unlink prevs of input pin %s which belong to %s\n", input_pin->name, input_pin->parent->name);
	mtfs_list_for_each_safe(p, tmp, &input_pin->prevs) {
		prev_pin = mtfs_list_entry(p, struct pin, pin_list);
		prev_component = prev_pin->parent;
		MDEBUG("doing of component %s\n", prev_component->name);
		componen_output_pin_unlink(prev_pin);
		if (ret) {
			goto out;
		}
		MDEBUG("done\n");
	}
out:
	MRETURN(ret);
}

int componen_remove_links(struct component *component)
{
	int ret = 0;
	MENTRY();
	if (component->pins) {
		int i = 0;
		for (i = 0; i < component->pin_num; i++) {
			if (component->pins[i].is_input) {
				componen_input_pin_unlink(&component->pins[i]);
			} else {
				componen_output_pin_unlink(&component->pins[i]);
			}
		}
	}
	MRETURN(ret);
}
/*
 * This is a recursive remove funtion.
 */
int component_remove(struct component *component)
{
	int ret = 0;
	MENTRY();

	if (component->is_graph) {
		ret = component_subs_eachdo(component, component_remove);
		if (ret) {
			goto out;
		}
	}

	componen_remove_links(component);
	if (component->type != NULL &&
	    component->type != component) {
		/* This is not a type */
		component_type_put(component->type);
	}

	component_list_delete(component);
	MDEBUG("removed %s from %s\n", component->name, component->parent->name);	
	component_free(component);

out:
	MRETURN(ret);
}

static void component_free_pins(struct component *component)
{
	int i = 0;
	MENTRY();

	for (i = 0; i < component->pin_num; i++) {
		if (component->pins[i].name) {
			MTFS_FREE_STR(component->pins[i].name);
		}
	}
	MTFS_FREE(component->pins, sizeof(*component->pins) * component->pin_num);
	component->pin_num = 0;

	_MRETURN();
}

int component_copy_pins(struct component *old_component, struct component *new_component)
{
	int i = 0;
	int ret = 0;
	MENTRY();

	MASSERT(old_component);
	MASSERT(new_component);
	MASSERT(old_component->pins);
	new_component->pin_num = old_component->pin_num;
	new_component->pins = component_alloc_pins(new_component->pin_num);
	for(i = 0; i < new_component->pin_num; i ++) {
		MTFS_STRDUP(new_component->pins[i].name, old_component->pins[i].name);
		if (new_component->pins[i].name == NULL) {
			ret = -ENOMEM;
			goto out_free_pins;
		}
		new_component->pins[i].is_input = old_component->pins[i].is_input;
		new_component->pins[i].parent = new_component;
		MTFS_INIT_LIST_HEAD(&new_component->pins[i].prevs);
	}
	goto out;
out_free_pins:
	component_free_pins(new_component);
out:
	MRETURN(ret);
}
/*
 * This is a recursive funtion.
 * If failed, this funtion may leaves some components undeleted which are conpied lately.
 */
struct component *component_copy(struct component *old_component, struct component *parent, const char *new_name)
{
	int ret = 0;
	struct component *new_component = NULL;
	MENTRY();

	if (old_component->type) {
		new_component = component_new(parent, old_component->type->name, new_name);
	} else {
		MASSERT(old_component->is_graph);
		new_component = component_new_notype(parent, new_name);
		if (IS_ERR(new_component)) {
			ret = PTR_ERR(new_component);
			goto out;
		}

		ret = component_copy_subs(old_component, new_component);
		if (ret) {
			goto out;
		}

		if (old_component->pins) {
			ret = component_copy_pins(old_component, new_component);
			if (ret) {
				goto out;
			}
		}
		/* TODO: How about links? */
	}
out:
	if (ret) {
		new_component = ERR_PTR(ret);
	}
	MRETURN(new_component);
}

char *component_path(struct component *component)
{
	char *path = NULL;
	char *parent = NULL;
	int length = 0;
	MENTRY();

	if (component->parent == component) {
		path = malloc(2);
		if (path == NULL) {
			goto out;
		}
		path[0] = '/';
		path[1] = 0;
	} else {
		parent = component_path(component->parent);
		if (parent == NULL) {
			goto out;
		}

		length = strlen(parent) + strlen(component->name) + 2;
		path = malloc(length);
		if (path == NULL) {
			goto out;
		}
		if (parent[strlen(parent) - 1] == '/') {
			snprintf(path, length, "%s%s", parent, component->name);
		} else {
			snprintf(path, length, "%s/%s", parent, component->name);
		}
		free(parent);
	}
out:
	MRETURN(path);
}

int component_print(struct component *component)
{
	const char *type = NULL;
	MENTRY();

	if (component->is_graph) {
		type = "graph";
	} else {
		type = "filter";
	}
	printf("%s (%s)\n", component->name, type);

	MRETURN(0);
}

struct component *component_search(struct component *parent, const char *name)
{
	struct component *found = NULL;
	struct mtfs_list_head *p = NULL;
	MENTRY();

	if (!parent->is_graph) {
		MERROR("Not a graph\n");
		found = ERR_PTR(-ENOMEM); /* which errno? */
		goto out;
	}

	mtfs_list_for_each(p, &parent->this_graph.subcomponents) {
		found = mtfs_list_entry(p, struct component, component_list);
		MASSERT(found->name);
		MASSERT(found);
		if (strcmp(found->name, name) == 0) {
			goto out;
		}
	}
	found = NULL;
out:
	MRETURN(found);
}

/* 
 * @func can delete current subgraph.
 */
int component_subs_eachdo(struct component *parent, int (*func)(struct component *subcomponent))
{
	struct component *found = NULL;
	struct mtfs_list_head *p = NULL;
	struct mtfs_list_head *tmp = NULL;
	int ret = 0;
	MENTRY();

	MASSERT(parent->is_graph);

	MDEBUG("eachdo subcomponents of %s\n", parent->name);
	mtfs_list_for_each_safe(p, tmp, &parent->this_graph.subcomponents) {
		found = mtfs_list_entry(p, struct component, component_list);
		MDEBUG("doing %s\n", found->name);
		ret = func(found);
		if (ret) {
			goto out;
		}
		MDEBUG("done\n");
	}
out:
	MRETURN(ret);
}

int component_copy_subs(struct component *old_parent, struct component *parent)
{
	struct component *new_sub = NULL;
	struct component *sub = NULL;
	struct mtfs_list_head *p = NULL;
	int ret = 0;
	MENTRY();

	MASSERT(parent->is_graph);
	MASSERT(old_parent->is_graph);

	MDEBUG("copy subcomponents of %s to %s\n", old_parent->name, parent->name);
	mtfs_list_for_each(p, &old_parent->this_graph.subcomponents) {
		sub = mtfs_list_entry(p, struct component, component_list);
		new_sub = component_copy(sub, parent, sub->name);
		if (IS_ERR(new_sub)) {
			ret = PTR_ERR(new_sub);
			goto out;
		}
	}
out:
	MRETURN(ret);
}

int component_insert(struct component *component, struct component *subcomponent)
{
	int ret = 0;
	struct component *found = NULL;
	MENTRY();

	if (!component->is_graph) {
		MERROR("Not a graph\n");
		ret = -ENOMEM; /* which errno? */
		goto out;
	}

	if (component->type) {
		MERROR("Have a type\n");
		ret = -ENOMEM; /* which errno? */
		goto out;
	}

	found = component_search(component, subcomponent->name);
	if (found) {
		MERROR("name %s has already been used in component %s\n",
		       subcomponent->name, component->name);
		ret = -EALREADY;
		goto out;
	}

	mtfs_list_add(&(subcomponent->component_list), &component->this_graph.subcomponents);
	subcomponent->parent = component;
out:
	MRETURN(ret);
}

struct pin *component_search_pin(struct component *component, const char *pin_name)
{
	struct pin *found = NULL;
	struct pin *pin = NULL;
	int i = 0;
	MENTRY();

	if (component->pins) {
		for (i = 0; i < component->pin_num; i++) {
			pin = &(component->pins[i]);
			if (strcmp(pin_name, pin->name) == 0) {
				found = pin;
				goto out;
			}
		}
	}
out:
	MRETURN(found);
}

int component_link(struct component *source_component, const char *source_pin_name,
                   struct component *dest_component, const char *dest_pin_name)
{
	int ret = 0;
	struct pin *source_pin = NULL;
	struct pin *dest_pin = NULL;
	MENTRY();

	MASSERT(source_component);
	MASSERT(dest_component);
	if (source_component == dest_component) {
		MERROR("try to link pins of the same compoent %s\n", source_component->name);
		ret = -ENOMEM; /* Which errno? */
		goto out;
	}

	source_pin = component_search_pin(source_component, source_pin_name);
	if (source_pin == NULL) {
		MERROR("compoent %s have no outpin called %s\n", source_component->name, source_pin_name);
		ret = -EINVAL;
		goto out;
	} else if (source_pin->is_input) {
		MERROR("pin %s of compoent %s is a input pin\n", source_pin_name, source_component->name);
		ret = -EINVAL;
		goto out;
	}

	dest_pin = component_search_pin(dest_component, dest_pin_name);
	if (dest_pin == NULL) {
		MERROR("compoent %s have no inpin called %s\n", dest_component->name, dest_pin_name);
		ret = -EINVAL;
		goto out;
	} else if (!dest_pin->is_input) {
		MERROR("pin %s of compoent %s is a output pin\n", dest_pin_name, dest_component->name);
		ret = -EINVAL;
		goto out;
	}

	if (source_pin->next) {
		MERROR("the pin %s of component %s is already linked to the pin %s of component %s\n",
		       source_pin_name, source_component->name, source_pin->next->name, source_pin->next->parent->name);
		ret = -ENOMEM; /* Which errno? */
		goto out;
	}

	mtfs_list_add(&(source_pin->pin_list), &dest_pin->prevs);
	source_pin->next = dest_pin;
out:
	MRETURN(ret);
}

int component_unlink(struct component *source_component, const char *source_pin_name,
                     struct component *dest_component, const char *dest_pin_name)
{
	int ret = 0;
	struct pin *source_pin = NULL;
	struct pin *dest_pin = NULL;
	MENTRY();

	MASSERT(source_component);
	MASSERT(dest_component);
	if (source_component == dest_component) {
		MERROR("try to unlink pins of the same compoent %s\n", source_component->name);
		ret = -ENOMEM; /* Which errno? */
		goto out;
	}

	source_pin = component_search_pin(source_component, source_pin_name);
	if (source_pin == NULL) {
		MERROR("compoent %s have no outpin called %s\n", source_component->name, source_pin_name);
		ret = -EINVAL;
		goto out;
	} else if (source_pin->is_input) {
		MERROR("pin %s of compoent %s is a input pin\n", source_pin_name, source_component->name);
		ret = -EINVAL;
		goto out;
	}

	dest_pin = component_search_pin(dest_component, dest_pin_name);
	if (dest_pin == NULL) {
		MERROR("compoent %s have no inpin called %s\n", dest_component->name, dest_pin_name);
		ret = -EINVAL;
		goto out;
	} else if (!dest_pin->is_input) {
		MERROR("pin %s of compoent %s is a output pin\n", dest_pin_name, dest_component->name);
		ret = -EINVAL;
		goto out;
	}

	if (source_pin->next == NULL) {
		MERROR("the pin %s of component %s is not linked to any component\n",
		       source_pin_name, source_component->name);
		ret = -ENOMEM; /* Which errno? */
		goto out;
	}

	ret = componen_output_pin_unlink(source_pin);
out:
	MRETURN(ret);
}

int component_pack(struct component *component, int table_number, struct pin_map_table *tables)
{
	int ret = 0;
	int i = 0;
	MENTRY();

	if (!component->is_graph) {
		MERROR("component %s is not a graph\n", component->name);
		ret = -EINVAL;
		goto out;
	}

	if (component->pins != NULL) {
		MERROR("component %s already have pins\n", component->name);
		ret = -EINVAL;
		goto out;
	}
	
	MASSERT(table_number > 0);

	component->pin_num = table_number;
	component->pins = component_alloc_pins(table_number);

	for (i = 0; i < table_number; i++) {
		struct component *sub_component = NULL;
		struct pin *pin = NULL;

		sub_component = component_search(component, tables[i].sub_component_name);
		if (sub_component == NULL) {
			MERROR("component %s have no subcomponent %s\n",
			       component->name, tables[i].sub_component_name);
			ret = -EINVAL;
			goto out_free_pins;
		}

		MDEBUG("searching %s\n", tables[i].sub_pin_name);
		pin = component_search_pin(sub_component, tables[i].sub_pin_name);
		if (pin == NULL) {
			MERROR("component %s have no pin %s\n", sub_component->name, tables[i].sub_pin_name);
			ret = -EINVAL;
			goto out_free_pins;
		}

		MTFS_STRDUP(component->pins[i].name, tables[i].pin_name);
		if (component->pins[i].name == NULL) {
			ret = -ENOMEM;
			goto out_free_pins;
		}
		component->pins[i].is_input = pin->is_input;
		component->pins[i].parent = component;
		MTFS_INIT_LIST_HEAD(&component->pins[i].prevs);
	}
	goto out;
out_free_pins:
	component_free_pins(component);
out:
	MRETURN(ret);
}
