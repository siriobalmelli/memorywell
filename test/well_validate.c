#include <well.h>
#include <zed_dbg.h>
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

	size_t pos, ret;
		
	/* single */
	ret = well_reserve(&buf->tx, &pos, 0);
	Z_err_if(ret, "reserve 0 returned %zu", ret);
	well_release_single(&buf->rx, 0);
	Z_err_if(memcmp(&state, buf, sizeof(state)), "nop must not alter state");

	/* multi */
	ret = well_reserve(&buf->rx, &pos, 0);
	Z_err_if(ret, "reserve 0 returned %zu", ret);
	ret = well_release_multi(&buf->tx, 0, pos);
	Z_err_if(ret, "release 0 returned %zu", ret);
	Z_err_if(memcmp(&state, buf, sizeof(state)), "nop must not alter state");

	return err_cnt;
}


/*	main()
*/
int main()
{
	int err_cnt = 0;
	struct well buf = { {0} };

	/* init buffer */
	Z_die_if(well_params(42, 10, &buf), "");
	Z_err_if(well_blk_size(&buf) != 64,
		"blk_size %zu is not next power of 2 above 42",
		well_blk_size(&buf));
	Z_err_if(well_blk_count(&buf) != 16,
		"blk_count %zu is not next power of 2 above 10",
		well_blk_count(&buf));

	/* allocate memory */
	Z_die_if(
		well_init(&buf, malloc(well_size(&buf)))
		, "size %zu", well_size(&buf));

	/* run tests */
	err_cnt += test_zero(&buf);

out:
	well_deinit(&buf);
	free(well_mem(&buf));
	return err_cnt;
}
