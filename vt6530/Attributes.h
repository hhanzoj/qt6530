/*
 *   This file is part of the Standard Portable Library (SPL).
 *
 *   SPL is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Foobar is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with SPL.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _attributes_
#define _attributes_

#define MASK_CHAR  (0xFF)

/**
 *  VIDEO attributes
 */
#define  VID_NORMAL		 (1<<8)
#define  VID_BLINKING	 (1<<9)
#define  VID_REVERSE	 (1<<10)
#define  VID_INVIS		 (1<<11)
#define  VID_UNDERLINE	 (1<<12)
#define  MASK_VID		 (VID_NORMAL|VID_BLINKING|VID_REVERSE|VID_INVIS|VID_UNDERLINE)

/**
 *  DATA attributes
 */
#define  DAT_MDT	  (1<<13)
#define  DAT_TYPE	  ((1<<14) | (1<<15) | (1<<16))
#define  DAT_AUTOTAB  (1<<17)
#define  DAT_UNPROTECT  (1<<18)
#define  SHIFT_DAT_TYPE  14

/**
 *  KEY attributes
 */
#define  KEY_UPSHIFT	 (1<<19)
#define  KEY_KB_ONLY	 (1<<20)
#define  KEY_AID_ONLY	 (1<<21)
#define  KEY_EITHER		 (1<<22)

/**
 *  SPECIAL attributes
 */
#define CHAR_START_FIELD  (1<<23)
#define CHAR_CELL_DIRTY  (1<<24)
#define MASK_FIELD  ((1<<8)|(1<<9)|(1<<10)|(1<<11)|(1<<12)|(1<<13)|(1<<14)|(1<<15)|(1<<16)|(1<<17)|(1<<18)|(1<<19)|(1<<20)|(1<<21)|(1<<22)|(1<<23)|(1<<25)|(1<<26)|(1<<27))

/**
 *  Color bits
 */
#define MASK_COLOR  ((1<<25)|(1<<26)|(1<<27))
#define SHIFT_COLOR  25

/**
 *  Insert mode 
 */
#define INSERT_INSERT  0
#define INSERT_OVERWRITE  1
	

#endif
