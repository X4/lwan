/*
 * lwan - simple web server
 * Copyright (c) 2012 Leandro A. F. Pereira <leandro@hardinfo.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdlib.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "lwan.h"

lwan_http_status_t
lua(lwan_request_t *request __attribute__((unused)),
    lwan_response_t *response __attribute__((unused)),
    void *data)
{
    lua_State *l;
    lwan_http_status_t status = HTTP_INTERNAL_ERROR;
    
    l = luaL_newstate();
    if (!l)
        return status;

    if (luaL_dofile(l, data) != LUA_OK)
        goto error;

    lua_getglobal(l, "main");
    if (lua_isfunction(l, -1)) {
        lua_pcall(l, 0, 1, 0);
        if (lua_isnumber(l, -1))
            status = lua_tonumber(l, -1);
    }
    lua_pop(l, 1);
    
error:
    lua_close(l);
    return status;
}
