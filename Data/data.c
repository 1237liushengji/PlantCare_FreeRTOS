#include "data.h"
#include "string.h"
#include "app_error.h"

PlantCare_t g_plant;

void Data_Init(void)
{
	memset(&g_plant, 0, sizeof(g_plant));

	g_plant.threshold.temp_h  = DEFAULT_TEMP_THRESHOLD;
	g_plant.threshold.humi_l  = DEFAULT_HUMIDITY_THRESHOLD;
	g_plant.threshold.soil_l  = DEFAULT_SOIL_THRESHOLD;
	g_plant.threshold.light_l = DEFAULT_LIGHT_THRESHOLD;

	g_plant.sys.run_mode = 0;
	g_plant.sys.set_cs = 0;
	g_plant.sys.set_cs_number = 0;
	g_plant.sys.connected = 0;
	g_plant.sys.error = ERR_OK;
	g_plant.sys.error_page = 0;

	Error_Init();
}
