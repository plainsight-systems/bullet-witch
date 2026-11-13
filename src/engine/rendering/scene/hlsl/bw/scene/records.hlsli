// The sizes of the scene's GPU records as a shader steps through their
// buffers: kDrawRecordBytes (bw/rendering/scene/draw_records.hpp) and
// kInstanceRecordBytes (bw/rendering/scene/instance.hpp). The generated
// include declares each record's struct and loader but not its size, so
// the sizes are stated once here for every shader that indexes a record
// buffer.
#ifndef BW_SCENE_RECORDS_HLSLI
#define BW_SCENE_RECORDS_HLSLI

#define BW_DRAW_RECORD_BYTES 48u
#define BW_INSTANCE_RECORD_BYTES 336u

#endif
