#pragma once

struct twzkv_item {
	void *data;
	size_t length;
};

struct object;
int twzkv_get(struct object *index, struct twzkv_item *key, struct twzkv_item *value);
int twzkv_put(struct object *index, struct object *data,
		struct twzkv_item *key, struct twzkv_item *value);
int twzkv_create_index(struct object *index);
int twzkv_create_data(struct object *data);
