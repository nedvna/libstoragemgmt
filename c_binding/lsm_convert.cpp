/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 */

#include "lsm_convert.hpp"
#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_optionaldata.h"
#include <libstoragemgmt/libstoragemgmt_blockrange.h>
#include <libstoragemgmt/libstoragemgmt_nfsexport.h>

static bool is_expected_object(Value &obj, std::string class_name)
{
    if (obj.valueType() == Value::object_t) {
        std::map<std::string, Value> i = obj.asObject();
        std::map<std::string, Value>::iterator iter = i.find("class");
        if (iter != i.end() && iter->second.asString() == class_name) {
            return true;
        }
    }
    return false;
}

lsm_volume *value_to_volume(Value &vol)
{
    lsm_volume *rc = NULL;

    if (is_expected_object(vol, "Volume")) {
        std::map<std::string, Value> v = vol.asObject();
        rc = lsm_volume_record_alloc(
            v["id"].asString().c_str(),
            v["name"].asString().c_str(),
            v["vpd83"].asString().c_str(),
            v["block_size"].asUint64_t(),
            v["num_of_blocks"].asUint64_t(),
            v["status"].asUint32_t(),
            v["system_id"].asString().c_str(),
            v["pool_id"].asString().c_str());
    }

    return rc;
}

Value volume_to_value(lsm_volume *vol)
{
    if( LSM_IS_VOL(vol) ) {
        std::map<std::string, Value> v;
        v["class"] = Value("Volume");
        v["id"] = Value(vol->id);
        v["name"] = Value(vol->name);
        v["vpd83"] = Value(vol->vpd83);
        v["block_size"] = Value(vol->block_size);
        v["num_of_blocks"] = Value(vol->number_of_blocks);
        v["status"] = Value(vol->status);
        v["system_id"] = Value(vol->system_id);
        v["pool_id"] = Value(vol->pool_id);
        return Value(v);
    }
    return Value();
}

int value_array_to_volumes(Value &volume_values, lsm_volume **volumes[],
                            uint32_t *count)
{
    int rc = LSM_ERR_OK;
    try {
        *count = 0;

        if( Value::array_t == volume_values.valueType()) {
            std::vector<Value> vol = volume_values.asArray();

            *count = vol.size();

            if( vol.size() ) {
                *volumes = lsm_volume_record_array_alloc(vol.size());

                if( *volumes ){
                    for( size_t i = 0; i < vol.size(); ++i ) {
                        (*volumes)[i] = value_to_volume(vol[i]);
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch( const ValueException &ve) {
        if( *volumes && *count ) {
            lsm_volume_record_array_free(*volumes, *count);
            *volumes = NULL;
            *count = 0;
        }

        rc = LSM_ERR_INTERNAL_ERROR;
    }
    return rc;
}

lsm_disk *value_to_disk(Value &disk)
{
    lsm_disk *rc = NULL;
    if (is_expected_object(disk, "Disk")) {
        lsm_optional_data *op = NULL;

        if( disk.asObject().find("optional_data") != disk.asObject().end() ) {
            Value opv = disk["optional_data"];
            op = value_to_optional_data(opv);
        }

        std::map<std::string, Value> d = disk.asObject();
        rc = lsm_disk_record_alloc(
            d["id"].asString().c_str(),
            d["name"].asString().c_str(),
            (lsm_disk_type)d["disk_type"].asInt32_t(),
            d["block_size"].asUint64_t(),
            d["num_of_blocks"].asUint64_t(),
            d["status"].asUint64_t(),
            op,
            d["system_id"].asString().c_str()
            );

        /* Optional data gets copied in lsmDiskRecordAlloc */
        lsm_optional_data_record_free(op);
    }
    return rc;
}


Value disk_to_value(lsm_disk *disk)
{
    if ( LSM_IS_DISK(disk) ) {
        std::map<std::string, Value> d;
        d["class"] = Value("Disk");
        d["id"] = Value(disk->id);
        d["name"] = Value(disk->name);
        d["disk_type"] = Value(disk->disk_type);
        d["block_size"] = Value(disk->block_size);
        d["num_of_blocks"] = Value(disk->block_count);
        d["status"] = Value(disk->disk_status);
        d["system_id"] = Value(disk->system_id);

        if( disk->optional_data ) {
            d["optional_data"] = optional_data_to_value(disk->optional_data);
        }

        return Value(d);
    }
    return Value();
}

int value_array_to_disks(Value &disk_values, lsm_disk **disks[], uint32_t *count)
{
    int rc = LSM_ERR_OK;
    try {
        *count = 0;

        if( Value::array_t == disk_values.valueType()) {
            std::vector<Value> d = disk_values.asArray();

            *count = d.size();

            if( d.size() ) {
                *disks = lsm_disk_record_array_alloc(d.size());

                if( *disks ){
                    for( size_t i = 0; i < d.size(); ++i ) {
                        (*disks)[i] = value_to_disk(d[i]);
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch( const ValueException &ve ) {
        rc = LSM_ERR_INTERNAL_ERROR;
        if( *disks && *count ) {
            lsm_disk_record_array_free(*disks, *count);
            *disks = NULL;
            *count = 0;
        }
    }
    return rc;
}

lsm_initiator *value_to_initiator(Value &init)
{
    lsm_initiator *rc = NULL;

    if (is_expected_object(init, "Initiator")) {
        std::map<std::string, Value> i = init.asObject();
        rc = lsm_initiator_record_alloc(
            (lsm_initiator_type) i["type"].asInt32_t(),
            i["id"].asString().c_str(),
            i["name"].asString().c_str()
            );
    }
    return rc;

}

Value initiator_to_value(lsm_initiator *init)
{
    if( LSM_IS_INIT(init) ) {
        std::map<std::string, Value> i;
        i["class"] = Value("Initiator");
        i["type"] = Value((int32_t) init->id_type);
        i["id"] = Value(init->id);
        i["name"] = Value(init->name);
        return Value(i);
    }
    return Value();
}

lsm_pool *value_to_pool(Value &pool)
{
    lsm_pool *rc = NULL;

    if (is_expected_object(pool, "Pool")) {
        std::map<std::string, Value> i = pool.asObject();
        rc = lsm_pool_record_alloc(i["id"].asString().c_str(),
            i["name"].asString().c_str(),
            i["total_space"].asUint64_t(),
            i["free_space"].asUint64_t(),
            i["status"].asUint64_t(),
			i["status_info"].asString().c_str(),
            i["system_id"].asString().c_str());
    }
    return rc;
}

Value pool_to_value(lsm_pool *pool)
{
    if( LSM_IS_POOL(pool) ) {
        std::map<std::string, Value> p;
        p["class"] = Value("Pool");
        p["id"] = Value(pool->id);
        p["name"] = Value(pool->name);
        p["total_space"] = Value(pool->total_space);
        p["free_space"] = Value(pool->free_space);
        p["status"] = Value(pool->status);
		p["status_info"] = Value(pool->status_info);
        p["system_id"] = Value(pool->system_id);
        return Value(p);
    }
    return Value();
}

lsm_system *value_to_system(Value &system)
{
    lsm_system *rc = NULL;
    if (is_expected_object(system, "System")) {
        std::map<std::string, Value> i = system.asObject();
        rc = lsm_system_record_alloc(  i["id"].asString().c_str(),
                                    i["name"].asString().c_str(),
                                    i["status"].asUint32_t(),
                                    i["status_info"].asString().c_str());
    }
    return rc;
}

Value system_to_value(lsm_system *system)
{
    if( LSM_IS_SYSTEM(system)) {
        std::map<std::string, Value> s;
        s["class"] = Value("System");
        s["id"] = Value(system->id);
        s["name"] = Value(system->name);
        s["status"] = Value(system->status);
        s["status_info"] = Value(system->status_info);
        return Value(s);
    }
    return Value();
}

lsm_string_list *value_to_string_list(Value &v)
{
    lsm_string_list *il = NULL;

    if( Value::array_t == v.valueType() ) {
        std::vector<Value> vl = v.asArray();
        uint32_t size = vl.size();
        il = lsm_string_list_alloc(size);

        if( il ) {
            for( uint32_t i = 0; i < size; ++i ) {
                if(LSM_ERR_OK != lsm_string_list_elem_set(il, i, vl[i].asC_str())){
                    lsm_string_list_free(il);
                    il = NULL;
                    break;
                }
            }
        }
    }
    return il;
}

Value string_list_to_value( lsm_string_list *sl)
{
    std::vector<Value> rc;
    if( LSM_IS_STRING_LIST(sl) ) {
        uint32_t size = lsm_string_list_size(sl);

        for(uint32_t i = 0; i < size; ++i ) {
            rc.push_back(Value(lsm_string_list_elem_get(sl, i)));
        }
    }
    return Value(rc);
}

lsm_access_group *value_to_access_group( Value &group )
{
    lsm_string_list *il = NULL;
    lsm_access_group *ag = NULL;

    if( is_expected_object(group, "AccessGroup")) {
        std::map<std::string, Value> vAg = group.asObject();

        il = value_to_string_list(vAg["initiators"]);

        if( il ) {
            ag = lsm_access_group_record_alloc(vAg["id"].asString().c_str(),
                                        vAg["name"].asString().c_str(),
                                        il,
                                        vAg["system_id"].asString().c_str());

            /* Initiator list is copied in AccessroupRecordAlloc */
            lsm_string_list_free(il);
        }
    }
    return ag;
}

Value access_group_to_value( lsm_access_group *group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        std::map<std::string, Value> ag;
        ag["class"] = Value("AccessGroup");
        ag["id"] = Value(group->id);
        ag["name"] = Value(group->name);
        ag["initiators"] = Value(string_list_to_value(group->initiators));
        ag["system_id"] = Value(group->system_id);
        return Value(ag);
    }
    return Value();
}

lsm_access_group **value_to_access_group_list( Value &group, uint32_t *count )
{
    lsm_access_group **rc = NULL;
    std::vector<Value> ag = group.asArray();

    *count = ag.size();

    if( *count ) {
        rc = lsm_access_group_record_array_alloc(*count);
        if( rc ) {
            uint32_t i;
            for(i = 0; i < *count; ++i ) {
                rc[i] = value_to_access_group(ag[i]);
                if( !rc[i] ) {
                    lsm_access_group_record_array_free(rc, i);
                    rc = NULL;
                    break;
                }
            }
        }
    }
    return rc;
}

Value access_group_list_to_value( lsm_access_group **group, uint32_t count)
{
    std::vector<Value> rc;

    if( group && count ) {
        uint32_t i;
        for( i = 0; i < count; ++i ) {
            rc.push_back(access_group_to_value(group[i]));
        }
    }
    return Value(rc);
}

lsm_block_range *value_to_block_range(Value &br)
{
    lsm_block_range *rc = NULL;
    if( is_expected_object(br, "BlockRange") ) {
        std::map<std::string, Value> range = br.asObject();

        rc = lsm_block_range_record_alloc(range["src_block"].asUint64_t(),
                                        range["dest_block"].asUint64_t(),
                                        range["block_count"].asUint64_t());
    }
    return rc;
}

Value block_range_to_value(lsm_block_range *br)
{
    if( LSM_IS_BLOCK_RANGE(br) ) {
        std::map<std::string, Value> r;
        r["class"] = Value("BlockRange");
        r["src_block"] = Value(br->source_start);
        r["dest_block"] = Value(br->dest_start);
        r["block_count"] = Value(br->block_count);
        return Value(r);
    }
    return Value();
}

lsm_block_range **value_to_block_range_list(Value &brl, uint32_t *count)
{
    lsm_block_range **rc = NULL;
    std::vector<Value> r = brl.asArray();
    *count = r.size();
    if( *count ) {
        rc = lsm_block_range_record_array_alloc(*count);
        if( rc ) {
            for( uint32_t i = 0; i < *count; ++i ) {
                rc[i] = value_to_block_range(r[i]);
                if( !rc[i] ) {
                    lsm_block_range_record_array_free(rc, i);
                    rc = NULL;
                    break;
                }
            }
        }
    }
    return rc;
}

Value block_range_list_to_value( lsm_block_range **brl, uint32_t count )
{
    std::vector<Value> r;
    if( brl && count) {
        uint32_t i = 0;
        for( i = 0; i < count; ++i ) {
            r.push_back(block_range_to_value(brl[i]));
        }
    }
    return Value(r);
}

lsm_fs *value_to_fs(Value &fs)
{
    lsm_fs *rc = NULL;
    if( is_expected_object(fs, "FileSystem") ) {
        std::map<std::string, Value> f = fs.asObject();
        rc = lsm_fs_record_alloc(f["id"].asString().c_str(),
                                f["name"].asString().c_str(),
                                f["total_space"].asUint64_t(),
                                f["free_space"].asUint64_t(),
                                f["pool_id"].asString().c_str(),
                                f["system_id"].asString().c_str());
    }
    return rc;
}

Value fs_to_value(lsm_fs *fs)
{
    if( LSM_IS_FS(fs) ) {
        std::map<std::string, Value> f;
        f["class"] = Value("FileSystem");
        f["id"] = Value(fs->id);
        f["name"] = Value(fs->name);
        f["total_space"] = Value(fs->total_space);
        f["free_space"] = Value(fs->free_space);
        f["pool_id"] = Value(fs->pool_id);
        f["system_id"] = Value(fs->system_id);
        return Value(f);
    }
    return Value();
}


lsm_fs_ss *value_to_ss(Value &ss)
{
    lsm_fs_ss *rc = NULL;
    if( is_expected_object(ss, "FsSnapshot") ) {
        std::map<std::string, Value> f = ss.asObject();
        rc = lsm_fs_ss_record_alloc(f["id"].asString().c_str(),
                                f["name"].asString().c_str(),
                                f["ts"].asUint64_t());
    }
    return rc;
}

Value ss_to_value(lsm_fs_ss *ss)
{
    if( LSM_IS_SS(ss) ) {
        std::map<std::string, Value> f;
        f["class"] = Value("FsSnapshot");
        f["id"] = Value(ss->id);
        f["name"] = Value(ss->name);
        f["ts"] = Value(ss->ts);
        return Value(f);
    }
    return Value();
}

lsm_nfs_export *value_to_nfs_export(Value &exp)
{
    lsm_nfs_export *rc = NULL;
    if( is_expected_object(exp, "NfsExport") ) {
        int ok = 0;
        lsm_string_list *root = NULL;
        lsm_string_list *rw = NULL;
        lsm_string_list *ro = NULL;

        std::map<std::string, Value> i = exp.asObject();

        /* Check all the arrays for successful allocation */
        root = value_to_string_list(i["root"]);
        if( root ) {
            rw = value_to_string_list(i["rw"]);
            if( rw ) {
                ro = value_to_string_list(i["ro"]);
                if( !ro ) {
                    lsm_string_list_free(rw);
                    lsm_string_list_free(root);
                    rw = NULL;
                    root = NULL;
                } else {
                    ok = 1;
                }
            } else {
                lsm_string_list_free(root);
                root = NULL;
            }
        }

        if( ok ) {
            rc = lsm_nfs_export_record_alloc(
                i["id"].asC_str(),
                i["fs_id"].asC_str(),
                i["export_path"].asC_str(),
                i["auth"].asC_str(),
                root,
                rw,
                ro,
                i["anonuid"].asUint64_t(),
                i["anongid"].asUint64_t(),
                i["options"].asC_str()
                );

            lsm_string_list_free(root);
            lsm_string_list_free(rw);
            lsm_string_list_free(ro);
        }
    }
    return rc;
}

Value nfs_export_to_value(lsm_nfs_export *exp)
{
    if( LSM_IS_NFS_EXPORT(exp) ) {
        std::map<std::string, Value> f;
        f["class"] = Value("NfsExport");
        f["id"] = Value(exp->id);
        f["fs_id"] = Value(exp->fs_id);
        f["export_path"] = Value(exp->export_path);
        f["auth"] = Value(exp->auth_type);
        f["root"] = Value(string_list_to_value(exp->root));
        f["rw"] = Value(string_list_to_value(exp->rw));
        f["ro"] = Value(string_list_to_value(exp->ro));
        f["anonuid"] = Value(exp->anonuid);
        f["anongid"] = Value(exp->anongid);
        f["options"] = Value(exp->options);
        return Value(f);
    }
    return Value();

}

lsm_storage_capabilities *value_to_capabilities(Value &exp)
{
    lsm_storage_capabilities *rc = NULL;
    if( is_expected_object(exp, "Capabilities") ) {
        const char *val = exp["cap"].asC_str();
        rc = lsm_capability_record_alloc(val);
    }
    return rc;
}

Value capabilities_to_value(lsm_storage_capabilities *cap)
{
    if( LSM_IS_CAPABILITIY(cap) ) {
        std::map<std::string, Value> c;
        char *t = capability_string(cap);
        c["class"] = Value("Capabilities");
        c["cap"] = Value(t);
        free(t);
        return Value(c);
    }
    return Value();
}

lsm_optional_data *value_to_optional_data(Value &op)
{
    lsm_optional_data *rc = NULL;
    if( is_expected_object(op, "OptionalData") ) {
        rc = lsm_optional_data_record_alloc();
        if ( rc ) {
            std::map<std::string, Value> v = op["values"].asObject();
            std::map<std::string, Value>::iterator itr;
            for(itr=v.begin(); itr != v.end(); ++itr) {
                if( LSM_ERR_OK != lsm_optional_data_string_set(rc,
                                            itr->first.c_str(),
                                            itr->second.asC_str())) {
                    lsm_optional_data_record_free(rc);
                    rc = NULL;
                    break;
                }
            }
        }
    }
    return rc;
}

Value optional_data_to_value(lsm_optional_data *op)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    if( LSM_IS_OPTIONAL_DATA(op) ) {
        std::map<std::string, Value> c;
        std::map<std::string, Value> embedded_values;

        g_hash_table_iter_init(&iter, op->data);
        while(g_hash_table_iter_next(&iter, &key, &value)) {
            embedded_values[(const char*)key] = Value((const char*)(value));
        }

        c["class"] = Value("OptionalData");
        c["values"] = Value(embedded_values);

        return Value(c);
    }
    return Value();
}