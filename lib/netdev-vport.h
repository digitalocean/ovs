/*
 * Copyright (c) 2010 Nicira Networks.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NETDEV_VPORT_H
#define NETDEV_VPORT_H 1

struct netdev;
struct netdev_stats;
struct odp_port;
struct shash;

void netdev_vport_register(void);

void netdev_vport_get_config(const struct netdev *, void *config);

int netdev_vport_get_stats(const struct netdev *, struct netdev_stats *);
int netdev_vport_set_stats(struct netdev *, const struct netdev_stats *);

#endif /* netdev-vport.h */
