#include "dev_status.h"

const char *dev_status_str(dev_status_t st)
{
  switch (st) {
    case DEV_OK:          return "OK";
    case DEV_ERR_NACK:    return "NACK";
    case DEV_ERR_TIMEOUT: return "TIMEOUT";
    case DEV_ERR_BUS:     return "BUS";
    case DEV_ERR_ID:      return "BAD_ID";
    case DEV_ERR_PARAM:   return "PARAM";
    case DEV_ERR_BUSY:    return "BUSY";
    case DEV_ERR_VERIFY:  return "VERIFY";
  }
  return "?";
}
