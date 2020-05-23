#define TEST_DISABLE_CACHE_FUNCS DAEDALUS_PROFILE(__FUNCTION__);



u32 OSHLE_osInvalICache_Mario()
{
TEST_DISABLE_CACHE_FUNCS
#ifdef DAEDALUS_ENABLE_DYNAREC
	u32 p = gGPR[REG_a0]._u32_0;
	u32 len = gGPR[REG_a1]._u32_0;

	if (len < 0x4000)
		CPU_InvalidateICacheRange(p, len);
	else
		CPU_InvalidateICache();
#endif

	return PATCH_RET_JR_RA;
}

u32 OSHLE_osInvalICache_Rugrats()
{
	return OSHLE_osInvalICache_Mario();
}


u32 OSHLE_osInvalDCache_Mario()
{
TEST_DISABLE_CACHE_FUNCS
	//u32 p = gGPR[REG_a0]._u32_0;
	//u32 len = gGPR[REG_a1]._u32_0;

	//Console_Print( "osInvalDCache(0x%08x, %d)", p, len);

	return PATCH_RET_JR_RA;
}
u32 OSHLE_osInvalDCache_Rugrats()
{
TEST_DISABLE_CACHE_FUNCS
	//u32 p = gGPR[REG_a0]._u32_0;
	//u32 len = gGPR[REG_a1]._u32_0;

	//Console_Print( "osInvalDCache(0x%08x, %d)", p, len);

	return PATCH_RET_JR_RA;
}


u32 OSHLE_osWritebackDCache_Mario()
{
TEST_DISABLE_CACHE_FUNCS
	//u32 p = gGPR[REG_a0]._u32_0;
	//u32 len = gGPR[REG_a1]._u32_0;

	//Console_Print( "osWritebackDCache(0x%08x, %d)", p, len);

	return PATCH_RET_JR_RA;
}
u32 OSHLE_osWritebackDCache_Rugrats()
{
TEST_DISABLE_CACHE_FUNCS
	//u32 p = gGPR[REG_a0]._u32_0;
	//u32 len = gGPR[REG_a1]._u32_0;

	//Console_Print( "osWritebackDCache(0x%08x, %d)", p, len);

	return PATCH_RET_JR_RA;
}


u32 OSHLE_osWritebackDCacheAll()
{
TEST_DISABLE_CACHE_FUNCS
	//Console_Print( "osWritebackDCacheAll()");

	return PATCH_RET_JR_RA;
}
