#include <well.h>
#include <ndebug.h>
#include <stdlib.h>


/*	test_zero()
Reserving and releasing 0 blocks should never alter state.
*/
int test_zero(struct well *buf)
{
	int err_cnt = 0;

	/* check initial state */
	struct well state;
	memcpy(&state, buf, sizeof(struct well));

	struct well_res res;
		
	/* single */
	res = well_reserve(&buf->tx, 0);
	NB_err_if(res.cnt, "reserve 0 returned %zu", res.cnt);
	well_release_single(&buf->rx, 0);
	NB_err_if(memcmp(&state, buf, sizeof(state)), "nop must not alter state");

	/* multi */
	res = well_reserve(&buf->rx, 0);
	NB_err_if(res.cnt, "reserve 0 returned %zu", res.cnt);
	size_t ret;
	ret = well_release_multi(&buf->tx, res);
	NB_err_if(ret, "release 0 returned %zu", ret);

	NB_err_if(memcmp(&state, buf, sizeof(state)), "nop must not alter state");

	return err_cnt;
}


/*	main()
*/
int main()
{
	int err_cnt = 0;
	struct well buf = { {0} };

	/* init buffer */
	NB_die_if(well_params(42, 10, &buf), "");
	NB_err_if(well_blk_size(&buf) != 64,
		"blk_size %zu is not next power of 2 above 42",
		well_blk_size(&buf));
	NB_err_if(well_blk_count(&buf) != 16,
		"blk_count %zu is not next power of 2 above 10",
		well_blk_count(&buf));

	/* allocate memory */
	NB_die_if(
		well_init(&buf, malloc(well_size(&buf)))
		, "size %zu", well_size(&buf));

	/* run tests */
	err_cnt += test_zero(&buf);

die:
	well_deinit(&buf);
	free(well_mem(&buf));
	return err_cnt;
}
