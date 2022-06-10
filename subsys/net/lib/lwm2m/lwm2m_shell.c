/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_MODULE_NAME net_lwm2m_shell
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h> //sscan

#include <kernel.h>
#include <net/lwm2m.h>
#include <shell/shell.h>

#define LWM2M_HELP_CMD "LwM2M commands"
#define LWM2M_HELP_SEND "LwM2M SEND operation\nsend [OPTION]... [PATH]...\n" \
	"-n\tSend as non-confirmable\n" \
	"Paths are inserted without leading '/'\n" \
	"Root-level operation is unsupported"
#define LWM2M_HELP_EXEC "Execute a resource \nexec [PATH]\n" \
	"Paths are inserted without leading '/'\n"
#define LWM2M_HELP_READ "Read value from LwM2M resource\nread [PATH] [OPTIONS]...\n" \
	"-s \tRead value as string(default)\n" \
	"-b \tRead value as boolean (1/0)\n" \
	"-uX\tRead value as uintX_t\n" \
	"-sX\tRead value as intX_t\n" \
	"-f \tRead value as float\n" \
	"Paths are inserted without leading '/'\n"
#define LWM2M_HELP_WRITE "Write into LwM2M resource\nwrite [PATH] [OPTIONS]... [VALUE]\n" \
	"-s \tValue as string(default)\n" \
	"-b \tValue as bool\n" \
	"-uX\tValue as uintX_t\n" \
	"-sX\tValue as intX_t\n" \
	"-f \tValue as float\n" \
	"Paths are inserted without leading '/'\n"

static int cmd_send(const struct shell *sh, size_t argc, char **argv)
{
	int ret = 0;
	struct lwm2m_ctx *ctx = lwm2m_rd_client_ctx();
	int path_cnt = argc - 1;
	bool confirmable = true;
	int ignore_cnt = 1; /* Subcmd + arguments preceding the path list */

	if (!ctx) {
		shell_error(sh, "no lwm2m context yet\n");
		return -ENOEXEC;
	}

	if (argc < 2) {
		shell_error(sh, "no arguments or path(s)\n");
		shell_help(sh);
		return -EINVAL;
	}

	if (strcmp(argv[1], "-n") == 0) {
		confirmable = false;
		path_cnt--;
		ignore_cnt++;
	}

	if ((argc - ignore_cnt) == 0) {
		shell_error(sh, "no path(s)\n");
		shell_help(sh);
		return -EINVAL;
	}

	for (int idx = ignore_cnt; idx < argc; idx++) {
		if (argv[idx][0] < '0' || argv[idx][0] > '9') {
			shell_error(sh, "invalid path: %s\n", argv[idx]);
			shell_help(sh);
			return -EINVAL;
		}
	}

	ret = lwm2m_engine_send(ctx, (const char **)&(argv[ignore_cnt]),
			path_cnt, confirmable);
	if (ret < 0) {
		shell_error(sh, "can't do send operation, request failed\n");
		return -ENOEXEC;
	}

	return 0;
}
//This struct is already defined in lwm2m_object.h around line 366
//Needed it here to access the res->execute_cb on line 124. 
//An alternative to this is to include lwm2m_object.h or lwm2m_engine.h
// (The latter is probably better as the func lwm2m_engine_get_resource is needed as well)
//Another idea is to implement a function like: lwm2m_engine_trigger_callback(const char *pathstr)
//In lwm2m_engine.h and forward declare it in lwm2m.h
#ifndef LWM2M_OBJECT_H_
struct lwm2m_engine_res {
	lwm2m_engine_get_data_cb_t		read_cb;
	lwm2m_engine_get_data_cb_t		pre_write_cb;
#if CONFIG_LWM2M_ENGINE_VALIDATION_BUFFER_SIZE > 0
	lwm2m_engine_set_data_cb_t		validate_cb;
#endif
	lwm2m_engine_set_data_cb_t		post_write_cb;
	lwm2m_engine_execute_cb_t		execute_cb;

	struct lwm2m_engine_res_inst *res_instances;
	uint16_t res_id;
	uint8_t  res_inst_count;
	bool multi_res_inst;
};
#endif

/*From lwm2m_engine.h on line 155*/
int lwm2m_engine_get_resource(const char *pathstr, struct lwm2m_engine_res **res);

static int cmd_exec(const struct shell *sh, size_t argc, char **argv) 
{
	struct lwm2m_ctx *ctx = lwm2m_rd_client_ctx();

	if (!ctx) {
		shell_error(sh, "no lwm2m context yet\n");
		return -ENOEXEC;
	}

	int ignore_cnt = 2; // Subcmd + PATH
	const char* pathstr = argv[1];
	struct lwm2m_engine_res *res = NULL;
	int ret = lwm2m_engine_get_resource(pathstr, &res);
	if (ret < 0)
	{
		shell_error(sh, "Couldn't execute resource\n");
		return ret;
	}
	if(res->execute_cb)
	{
		uint16_t obj_id;
		sscanf(pathstr, "%*d/%hd/%*d", &obj_id);
		return res->execute_cb(obj_id, argv[ignore_cnt], argc-ignore_cnt);
	}
	return 0;
}

static int cmd_read(const struct shell *sh, size_t argc, char **argv) 
{
	
	struct lwm2m_ctx *ctx = lwm2m_rd_client_ctx();
	if (!ctx) {
		shell_error(sh, "no lwm2m context yet\n");
		return -ENOEXEC;
	}

	if (argc < 2) {
		shell_error(sh, "no arguments or path(s)\n");
		shell_help(sh);
		return -EINVAL;
	}

	const char* dtype = "string"; //default
	if (argc > 2) { // read + path + options(data type)
		dtype = argv[2];
	}

	const char* pathstr = argv[1];
	int ret = 0;
	
	if (strcmp(dtype, "string") == 0){
		const char* buff;
		uint16_t buff_len=0;
		uint8_t buff_flags;
		ret = lwm2m_engine_get_res_data(pathstr, (void **)&buff, &buff_len, &buff_flags);
		shell_print(sh, "%s\n", buff);
	} else if (strcmp(dtype, "-s8") == 0){
		int8_t temp=0;
		ret = lwm2m_engine_get_s8(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	} else if (strcmp(dtype, "-s16") == 0){
		int16_t temp=0;
		ret = lwm2m_engine_get_s16(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	} else if (strcmp(dtype, "-s32") == 0){
		int32_t temp=0;
		ret = lwm2m_engine_get_s32(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	} else if (strcmp(dtype, "-s64") == 0){
		int64_t temp=0;
		ret = lwm2m_engine_get_s64(pathstr, &temp);
		shell_print(sh, "%lld\n", temp);
	} else if (strcmp(dtype, "-u8") == 0){
		uint8_t temp=0;
		ret = lwm2m_engine_get_u8(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	} else if (strcmp(dtype, "-u16") == 0){
		uint16_t temp=0;
		ret = lwm2m_engine_get_u16(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	} else if (strcmp(dtype, "-u32") == 0){
		uint32_t temp=0;
		ret = lwm2m_engine_get_u32(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	} else if (strcmp(dtype, "-u64") == 0){
		uint64_t temp=0;
		ret = lwm2m_engine_get_u64(pathstr, &temp);
		shell_print(sh, "%lld\n", temp);
	} else if (strcmp(dtype, "-f") == 0){
		double temp=0;
		ret = lwm2m_engine_get_float(pathstr, &temp);
		shell_print(sh, "%f\n", temp);
	} else if (strcmp(dtype, "-b") == 0){
		bool temp; 
		ret = lwm2m_engine_get_bool(pathstr, &temp);
		shell_print(sh, "%d\n", temp);
	}else {
		shell_error(sh, "can't recognize data type %s\n", dtype);
		shell_help(sh);
		return -EINVAL;
	}
	

	if (ret < 0) {
		shell_error(sh, "can't do read operation, request failed\n");
		return ret;
	}
	return 0;
}

static int cmd_write(const struct shell *sh, size_t argc, char **argv) 
{
	struct lwm2m_ctx *ctx = lwm2m_rd_client_ctx();
	if (!ctx) {
		shell_error(sh, "no lwm2m context yet\n");
		return -ENOEXEC;
	}
	if (argc < 3) {
		shell_error(sh, "no arguments or path(s)\n");
		shell_help(sh);
		return -EINVAL;
	}

	int ret = 0;
	const char* pathstr = argv[1];

	if (argc == 3)  //write path value (string)
	{
		ret = lwm2m_engine_set_string(pathstr, argv[2]);
	}else if (argc==4) //0:write 1:path 2:options 3:value
	{
		const char* dtype = argv[2];
		int validx = 3; //value index
		char* e;
		if (strcmp(dtype, "-s8") == 0){
			ret = lwm2m_engine_set_s8(pathstr, strtol(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-s16") == 0){
			ret = lwm2m_engine_set_s16(pathstr, strtol(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-s32") == 0){
			ret = lwm2m_engine_set_s32(pathstr, strtol(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-s64") == 0){
			ret = lwm2m_engine_set_s64(pathstr, strtoll(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-u8" ) == 0){
			ret = lwm2m_engine_set_u8(pathstr, strtoul(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-u16") == 0){
			ret = lwm2m_engine_set_u16(pathstr, strtoul(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-u32") == 0){
			ret = lwm2m_engine_set_u32(pathstr, strtoul(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-u64") == 0){
			ret = lwm2m_engine_set_u64(pathstr, strtoull(argv[validx], &e, 10));
		} else if (strcmp(dtype, "b") == 0){
			ret = lwm2m_engine_set_bool(pathstr, strtoul(argv[validx], &e, 10));
		} else if (strcmp(dtype, "-f") == 0){
			double temp=strtod(argv[validx], &e);
			ret = lwm2m_engine_set_float(pathstr, &temp);
		}else {
			shell_error(sh, "can't recognize data type %s\n", dtype);
			shell_help(sh);
			return -EINVAL;
		}
		if (*e != '\0')
		{
			shell_error(sh, "Invalid number %s\n", argv[validx]);
			shell_help(sh);
			return -EINVAL;
		}
	}

	if (ret < 0) {
		shell_error(sh, "can't do write operation, request failed: %d\n", ret);
		return ret;
	}
	return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(sub_lwm2m,
			       SHELL_COND_CMD_ARG(CONFIG_LWM2M_VERSION_1_1, send, NULL,
						  LWM2M_HELP_SEND, cmd_send, 1, 9),
				   SHELL_COND_CMD_ARG(CONFIG_LWM2M_VERSION_1_1, exec, NULL,
				   		  LWM2M_HELP_EXEC, cmd_exec, 2, 9),
				   SHELL_COND_CMD_ARG(CONFIG_LWM2M_VERSION_1_1, read, NULL,
				   		  LWM2M_HELP_READ, cmd_read, 2, 1),
				   SHELL_COND_CMD_ARG(CONFIG_LWM2M_VERSION_1_1, write, NULL,
				   		  LWM2M_HELP_WRITE, cmd_write, 3, 1),
			       SHELL_SUBCMD_SET_END);

SHELL_COND_CMD_ARG_REGISTER(CONFIG_LWM2M_SHELL, lwm2m, &sub_lwm2m, LWM2M_HELP_CMD, NULL, 1, 0);
