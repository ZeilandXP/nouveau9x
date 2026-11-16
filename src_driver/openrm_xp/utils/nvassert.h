#ifndef NVX_OPENRM_XP_ASSERT_H
#define NVX_OPENRM_XP_ASSERT_H
#include <nvport/nvport.h>
#define NV_ASSERT(expr) do { if(!(expr)) nvx_rm_assert(__FILE__,__LINE__); } while(0)
#define NV_ASSERT_OR_RETURN(expr,status) do { if(!(expr)) { nvx_rm_assert(__FILE__,__LINE__); return (status); } } while(0)
#define NV_ASSERT_OK_OR_RETURN(expr) do { NV_STATUS nvx_status=(expr); if(nvx_status!=NV_OK) return nvx_status; } while(0)
#define NV_ASSERT_CHECKED(expr) NV_ASSERT(expr)
#define NV_ASSERT_FAILED(message) nvx_rm_assert(__FILE__,__LINE__)
#endif
