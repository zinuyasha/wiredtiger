/* DO NOT EDIT: automatically built by dist/config.py. */

#include "wt_internal.h"

const char *
__wt_confdfl_colgroup_meta =
    "columns=(),filename=""";

const char *
__wt_confchk_colgroup_meta =
    "columns=(type=list),filename=()";

const char *
__wt_confdfl_connection_add_collator =
    "";

const char *
__wt_confchk_connection_add_collator =
    "";

const char *
__wt_confdfl_connection_add_compressor =
    "";

const char *
__wt_confchk_connection_add_compressor =
    "";

const char *
__wt_confdfl_connection_add_cursor_type =
    "";

const char *
__wt_confchk_connection_add_cursor_type =
    "";

const char *
__wt_confdfl_connection_add_extractor =
    "";

const char *
__wt_confchk_connection_add_extractor =
    "";

const char *
__wt_confdfl_connection_close =
    "";

const char *
__wt_confchk_connection_close =
    "";

const char *
__wt_confdfl_connection_load_extension =
    "entry=wiredtiger_extension_init,prefix=""";

const char *
__wt_confchk_connection_load_extension =
    "entry=(),prefix=()";

const char *
__wt_confdfl_connection_open_session =
    "";

const char *
__wt_confchk_connection_open_session =
    "";

const char *
__wt_confdfl_cursor_close =
    "";

const char *
__wt_confchk_cursor_close =
    "";

const char *
__wt_confdfl_file_meta =
    "allocation_size=512B,block_compressor="",checksum=true,collator="","
    "columns=(),huffman_key="",huffman_value="",internal_item_max=0,"
    "internal_key_truncate=true,internal_page_max=2KB,key_format=u,key_gap=10"
    ",leaf_item_max=0,leaf_page_max=1MB,prefix_compression=true,root="","
    "split_pct=75,type=btree,value_format=u,version=(major=0,minor=0)";

const char *
__wt_confchk_file_meta =
    "allocation_size=(type=int,min=512B,max=128MB),block_compressor=(),"
    "checksum=(type=boolean),collator=(),columns=(type=list),huffman_key=(),"
    "huffman_value=(),internal_item_max=(type=int,min=0),"
    "internal_key_truncate=(type=boolean),internal_page_max=(type=int,"
    "min=512B,max=512MB),key_format=(type=format),key_gap=(type=int,min=0),"
    "leaf_item_max=(type=int,min=0),leaf_page_max=(type=int,min=512B,"
    "max=512MB),prefix_compression=(type=boolean),root=(),split_pct=(type=int"
    ",min=25,max=100),type=(choices=[\"btree\"]),value_format=(type=format),"
    "version=()";

const char *
__wt_confdfl_index_meta =
    "columns=(),filename=""";

const char *
__wt_confchk_index_meta =
    "columns=(type=list),filename=()";

const char *
__wt_confdfl_session_begin_transaction =
    "isolation=read-committed,name="",priority=0,sync=full";

const char *
__wt_confchk_session_begin_transaction =
    "isolation=(choices=[\"serializable\",\"snapshot\",\"read-committed\","
    "\"read-uncommitted\"]),name=(),priority=(type=int,min=-100,max=100),"
    "sync=(choices=[\"full\",\"flush\",\"write\",\"none\"])";

const char *
__wt_confdfl_session_checkpoint =
    "archive=false,flush_cache=true,flush_log=true,force=false,log_size=0,"
    "timeout=0";

const char *
__wt_confchk_session_checkpoint =
    "archive=(type=boolean),flush_cache=(type=boolean),"
    "flush_log=(type=boolean),force=(type=boolean),log_size=(type=int,min=0),"
    "timeout=(type=int,min=0)";

const char *
__wt_confdfl_session_close =
    "";

const char *
__wt_confchk_session_close =
    "";

const char *
__wt_confdfl_session_commit_transaction =
    "";

const char *
__wt_confchk_session_commit_transaction =
    "";

const char *
__wt_confdfl_session_create =
    "allocation_size=512B,block_compressor="",checksum=true,colgroups=(),"
    "collator="",columns=(),columns=(),exclusive=false,filename="","
    "huffman_key="",huffman_value="",internal_item_max=0,"
    "internal_key_truncate=true,internal_page_max=2KB,key_format=u,"
    "key_format=u,key_gap=10,leaf_item_max=0,leaf_page_max=1MB,"
    "prefix_compression=true,split_pct=75,type=btree,value_format=u,"
    "value_format=u";

const char *
__wt_confchk_session_create =
    "allocation_size=(type=int,min=512B,max=128MB),block_compressor=(),"
    "checksum=(type=boolean),colgroups=(type=list),collator=(),"
    "columns=(type=list),columns=(type=list),exclusive=(type=boolean),"
    "filename=(),huffman_key=(),huffman_value=(),internal_item_max=(type=int,"
    "min=0),internal_key_truncate=(type=boolean),internal_page_max=(type=int,"
    "min=512B,max=512MB),key_format=(type=format),key_format=(type=format),"
    "key_gap=(type=int,min=0),leaf_item_max=(type=int,min=0),"
    "leaf_page_max=(type=int,min=512B,max=512MB),"
    "prefix_compression=(type=boolean),split_pct=(type=int,min=25,max=100),"
    "type=(choices=[\"btree\"]),value_format=(type=format),"
    "value_format=(type=format)";

const char *
__wt_confdfl_session_drop =
    "force=false";

const char *
__wt_confchk_session_drop =
    "force=(type=boolean)";

const char *
__wt_confdfl_session_dumpfile =
    "";

const char *
__wt_confchk_session_dumpfile =
    "";

const char *
__wt_confdfl_session_log_printf =
    "";

const char *
__wt_confchk_session_log_printf =
    "";

const char *
__wt_confdfl_session_open_cursor =
    "append=false,bulk=false,clear_on_close=false,dump="","
    "isolation=read-committed,overwrite=false,raw=false,statistics=false";

const char *
__wt_confchk_session_open_cursor =
    "append=(type=boolean),bulk=(type=boolean),clear_on_close=(type=boolean),"
    "dump=(choices=[\"hex\",\"print\"]),isolation=(choices=[\"snapshot\","
    "\"read-committed\",\"read-uncommitted\"]),overwrite=(type=boolean),"
    "raw=(type=boolean),statistics=(type=boolean)";

const char *
__wt_confdfl_session_rename =
    "";

const char *
__wt_confchk_session_rename =
    "";

const char *
__wt_confdfl_session_rollback_transaction =
    "";

const char *
__wt_confchk_session_rollback_transaction =
    "";

const char *
__wt_confdfl_session_salvage =
    "force=false";

const char *
__wt_confchk_session_salvage =
    "force=(type=boolean)";

const char *
__wt_confdfl_session_sync =
    "";

const char *
__wt_confchk_session_sync =
    "";

const char *
__wt_confdfl_session_truncate =
    "";

const char *
__wt_confchk_session_truncate =
    "";

const char *
__wt_confdfl_session_upgrade =
    "";

const char *
__wt_confchk_session_upgrade =
    "";

const char *
__wt_confdfl_session_verify =
    "";

const char *
__wt_confchk_session_verify =
    "";

const char *
__wt_confdfl_table_meta =
    "colgroups=(),columns=(),key_format=u,value_format=u";

const char *
__wt_confchk_table_meta =
    "colgroups=(type=list),columns=(type=list),key_format=(type=format),"
    "value_format=(type=format)";

const char *
__wt_confdfl_wiredtiger_open =
    "buffer_alignment=-1,cache_size=100MB,create=false,direct_io=(),"
    "error_prefix="",eviction_target=80,eviction_trigger=95,extensions=(),"
    "hazard_max=30,home_environment=false,home_environment_priv=false,"
    "logging=false,multiprocess=false,session_max=50,transactional=false,"
    "verbose=()";

const char *
__wt_confchk_wiredtiger_open =
    "buffer_alignment=(type=int,min=-1,max=1MB),cache_size=(type=int,min=1MB,"
    "max=10TB),create=(type=boolean),direct_io=(type=list,choices=[\"data\","
    "\"log\"]),error_prefix=(),eviction_target=(type=int,min=10,max=99),"
    "eviction_trigger=(type=int,min=10,max=99),extensions=(type=list),"
    "hazard_max=(type=int,min=15),home_environment=(type=boolean),"
    "home_environment_priv=(type=boolean),logging=(type=boolean),"
    "multiprocess=(type=boolean),session_max=(type=int,min=1),"
    "transactional=(type=boolean),verbose=(type=list,choices=[\"block\","
    "\"evict\",\"evictserver\",\"fileops\",\"hazard\",\"mutex\",\"read\","
    "\"readserver\",\"reconcile\",\"salvage\",\"verify\",\"write\"])";
