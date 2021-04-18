struct lru_fsm_t cache_fsm_2way[] = {
/* Header */	{ 2,   2 }, /* Ways, Number of states */
/*  0 */	{ 0,   0*2 }, /* 0-1 */
		{ 1,   1*2 }, /* 1-0 */
/*  1 */	{ 1,   1*2 }, /* 1-0 */
		{ 0,   0*2 }, /* 0-1 */
};
