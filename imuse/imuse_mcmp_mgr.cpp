// Residual - Virtual machine to run LucasArts' 3D adventure games
// Copyright (C) 2003-2004 The ScummVM-Residual Team (www.scummvm.org)
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

#include "../stdafx.h"
#include "../bits.h"
#include "../debug.h"
#include "../timer.h"
#include "../resource.h"

#include "../mixer/mixer.h"
#include "../mixer/audiostream.h"

#include "imuse_mcmp_mgr.h"
#include "imuse_sndmgr.h"

uint16 imuseDestTable[5786];

McmpMgr::McmpMgr() {
	_compTable = NULL;
	_numCompItems = 0;
	_curSample = -1;
	_compInput = NULL;
}

McmpMgr::~McmpMgr() {
	_numCompItems = 0;
	_compTableLoaded = false;
	_lastBlock = -1;
	_outputSize = 0;
	_curSample = -1;
	free(_compTable);
	_compTable = NULL;
	free(_compInput);
	_compInput = NULL;
}

bool McmpMgr::openSound(const char *filename, byte *ptr) {
	_file = ResourceLoader::instance()->openNewStream(filename);

	if (!_file) {
		warning("McmpMgr::openFile() Can't open mcmp file: %s", filename);
		return false;
	}

	_outputSize = 0;
	_lastBlock = -1;

	loadCompTable(ptr);
}

void McmpMgr::loadCompTable(byte *ptr) {
	uint32 tag;
	int i;

	fread(&tag, 1, 4, _file);
	tag = MKID_BE32(tag);
	if (tag != MKID_BE('MCMP')) {
		error("McmpMgr::loadCompTable() Compressed sound %d invalid (%s)", index, tag2str(tag));
	}

	fread(&_numCompItems, 1, 2, _file);
	_numCompItems = MKID_BE16(_numCompItems);
	assert(_numCompItems > 0);

	int offset = ftell(_file) + (_numCompItems * 9) + 8;
	_numCompItems--;
	_compTable = (CompTable *)malloc(sizeof(CompTable) * _numCompItems);
	int32 header_size;
	fseek(_file, 5, SEEK_CUR);
	fread(_compTable[i].decompSize, 1, 4, _file);

	int32 maxSize = 0;
	for (i = 0; i < _numCompItems; i++) {
		fread(_compTable[i].codec, 1, 1, _file);
		fread(_compTable[i].decompSize, 1, 4, _file);
		_compTable[i].decompSize = MKID_BE32(_compTable[i].decompSize);
		fread(_compTable[i].compSize, 1, 4, _file);
		_compTable[i].compSize = MKID_BE32(_compTable[i].compSize);
		_compTable[i].offset = offset;
		offset += _compTable[i].compSize;
		if (_compTable[i].size > maxSize)
			maxSize = _compTable[i].compSize;
	}
	int16 sizeCodecs;
	fread(&sizeCodecs, 1, 2, _file);
	sizeCodecs = MKID_BE16(sizeCodecs);
	for (i = 0; i < _numCompItems; i++) {
		_compTable[i].offset += sizeCodecs;
	}
	fseek(_file, sizeCodecs, SEEK_CUR);
	_compInput = (byte *)malloc(maxSize);
	fread(_compInput, 1, _compTable[0].decompSize);
	*ptr = _compInput;
}

int32 McmpMgr::decompressSample(int32 offset, int32 size, byte **comp_final) {
	int32 i, final_size, output_size;
	int skip, first_block, last_block;

	if (!_file) {
		warning("McmpMgr::decompressSampleByName() File is not open!");
		return 0;
	}

	if (!_compTableLoaded) {
		return 0;
	}

	first_block = offset / 0x2000;
	last_block = (offset + size - 1) / 0x2000;

	// Clip last_block by the total number of blocks (= "comp items")
	if ((last_block >= _numCompItems) && (_numCompItems > 0))
		last_block = _numCompItems - 1;

	int32 blocks_final_size = 0x2000 * (1 + last_block - first_block);
	*comp_final = (byte *)malloc(blocks_final_size);
	final_size = 0;

	skip = (offset + header_size) % 0x2000;

	for (i = first_block; i <= last_block; i++) {
		if (_lastBlock != i) {
			_file.seek(_compTable[i].offset, SEEK_SET);
			_file.read(_compInput, _compTable[i].compSize);
			decompressVima(_compInput, (int16 *)_compOutput, _compTable[i].decompSize, imuseDestTable);
			_outputSize = _compTable[i].decompSize;
			if (_outputSize > 0x2000) {
				error("_outputSize: %d", _outputSize);
			}
			_lastBlock = i;
		}

		output_size = _outputSize - skip;

		if ((output_size + skip) > 0x2000) // workaround
			output_size -= (output_size + skip) - 0x2000;

		if (output_size > size)
			output_size = size;

		assert(final_size + output_size <= blocks_final_size);

		memcpy(*comp_final + final_size, _compOutput + skip, output_size);
		final_size += output_size;

		size -= output_size;
		assert(size >= 0);
		if (size == 0)
			break;

		skip = 0;
	}

	return final_size;
}