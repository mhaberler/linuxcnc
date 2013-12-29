/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010-2013 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "private-libwebsockets.h"

#ifdef WIN32
#include <io.h>
#endif


unsigned char lextable[] = {
/* pos 0: state 0 */
   0x47 /* 'G' */, 0x0A /* to pos 20 state 1 */,
   0x50 /* 'P' */, 0x0D /* to pos 28 state 5 */,
   0x4F /* 'O' */, 0x11 /* to pos 38 state 10 */,
   0x48 /* 'H' */, 0x19 /* to pos 56 state 18 */,
   0x43 /* 'C' */, 0x1E /* to pos 68 state 23 */,
   0x53 /* 'S' */, 0x29 /* to pos 92 state 34 */,
   0x55 /* 'U' */, 0x4F /* to pos 170 state 64 */,
   0x0D /* '.' */, 0x62 /* to pos 210 state 84 */,
   0x49 /* 'I' */, 0x7E /* to pos 268 state 113 */,
   0xC1 /* 'A' */, 0x9D /* to pos 332 state 144 */,
/* pos 20: state 1 */
   0xC5 /* 'E' */, 0x01 /* to pos 22 state 2 */,
/* pos 22: state 2 */
   0xD4 /* 'T' */, 0x01 /* to pos 24 state 3 */,
/* pos 24: state 3 */
   0xA0 /* ' ' */, 0x01 /* to pos 26 state 4 */,
/* pos 26: state 4 */
   0x80, 0x00 /* terminal marker */,
/* pos 28: state 5 */
   0xCF /* 'O' */, 0x01 /* to pos 30 state 6 */,
/* pos 30: state 6 */
   0xD3 /* 'S' */, 0x01 /* to pos 32 state 7 */,
/* pos 32: state 7 */
   0xD4 /* 'T' */, 0x01 /* to pos 34 state 8 */,
/* pos 34: state 8 */
   0xA0 /* ' ' */, 0x01 /* to pos 36 state 9 */,
/* pos 36: state 9 */
   0x81, 0x00 /* terminal marker */,
/* pos 38: state 10 */
   0x50 /* 'P' */, 0x02 /* to pos 42 state 11 */,
   0xF2 /* 'r' */, 0x49 /* to pos 186 state 72 */,
/* pos 42: state 11 */
   0xD4 /* 'T' */, 0x01 /* to pos 44 state 12 */,
/* pos 44: state 12 */
   0xC9 /* 'I' */, 0x01 /* to pos 46 state 13 */,
/* pos 46: state 13 */
   0xCF /* 'O' */, 0x01 /* to pos 48 state 14 */,
/* pos 48: state 14 */
   0xCE /* 'N' */, 0x01 /* to pos 50 state 15 */,
/* pos 50: state 15 */
   0xD3 /* 'S' */, 0x01 /* to pos 52 state 16 */,
/* pos 52: state 16 */
   0xA0 /* ' ' */, 0x01 /* to pos 54 state 17 */,
/* pos 54: state 17 */
   0x82, 0x00 /* terminal marker */,
/* pos 56: state 18 */
   0x6F /* 'o' */, 0x02 /* to pos 60 state 19 */,
   0xD4 /* 'T' */, 0xB5 /* to pos 420 state 188 */,
/* pos 60: state 19 */
   0xF3 /* 's' */, 0x01 /* to pos 62 state 20 */,
/* pos 62: state 20 */
   0xF4 /* 't' */, 0x01 /* to pos 64 state 21 */,
/* pos 64: state 21 */
   0xBA /* ':' */, 0x01 /* to pos 66 state 22 */,
/* pos 66: state 22 */
   0x83, 0x00 /* terminal marker */,
/* pos 68: state 23 */
   0xEF /* 'o' */, 0x01 /* to pos 70 state 24 */,
/* pos 70: state 24 */
   0xEE /* 'n' */, 0x01 /* to pos 72 state 25 */,
/* pos 72: state 25 */
   0x6E /* 'n' */, 0x02 /* to pos 76 state 26 */,
   0xF4 /* 't' */, 0x6F /* to pos 296 state 127 */,
/* pos 76: state 26 */
   0xE5 /* 'e' */, 0x01 /* to pos 78 state 27 */,
/* pos 78: state 27 */
   0xE3 /* 'c' */, 0x01 /* to pos 80 state 28 */,
/* pos 80: state 28 */
   0xF4 /* 't' */, 0x01 /* to pos 82 state 29 */,
/* pos 82: state 29 */
   0xE9 /* 'i' */, 0x01 /* to pos 84 state 30 */,
/* pos 84: state 30 */
   0xEF /* 'o' */, 0x01 /* to pos 86 state 31 */,
/* pos 86: state 31 */
   0xEE /* 'n' */, 0x01 /* to pos 88 state 32 */,
/* pos 88: state 32 */
   0xBA /* ':' */, 0x01 /* to pos 90 state 33 */,
/* pos 90: state 33 */
   0x84, 0x00 /* terminal marker */,
/* pos 92: state 34 */
   0xE5 /* 'e' */, 0x01 /* to pos 94 state 35 */,
/* pos 94: state 35 */
   0xE3 /* 'c' */, 0x01 /* to pos 96 state 36 */,
/* pos 96: state 36 */
   0xAD /* '-' */, 0x01 /* to pos 98 state 37 */,
/* pos 98: state 37 */
   0xD7 /* 'W' */, 0x01 /* to pos 100 state 38 */,
/* pos 100: state 38 */
   0xE5 /* 'e' */, 0x01 /* to pos 102 state 39 */,
/* pos 102: state 39 */
   0xE2 /* 'b' */, 0x01 /* to pos 104 state 40 */,
/* pos 104: state 40 */
   0xD3 /* 'S' */, 0x01 /* to pos 106 state 41 */,
/* pos 106: state 41 */
   0xEF /* 'o' */, 0x01 /* to pos 108 state 42 */,
/* pos 108: state 42 */
   0xE3 /* 'c' */, 0x01 /* to pos 110 state 43 */,
/* pos 110: state 43 */
   0xEB /* 'k' */, 0x01 /* to pos 112 state 44 */,
/* pos 112: state 44 */
   0xE5 /* 'e' */, 0x01 /* to pos 114 state 45 */,
/* pos 114: state 45 */
   0xF4 /* 't' */, 0x01 /* to pos 116 state 46 */,
/* pos 116: state 46 */
   0xAD /* '-' */, 0x01 /* to pos 118 state 47 */,
/* pos 118: state 47 */
   0x4B /* 'K' */, 0x08 /* to pos 134 state 48 */,
   0x50 /* 'P' */, 0x10 /* to pos 152 state 55 */,
   0x44 /* 'D' */, 0x26 /* to pos 198 state 78 */,
   0x56 /* 'V' */, 0x2E /* to pos 216 state 87 */,
   0x4F /* 'O' */, 0x35 /* to pos 232 state 95 */,
   0x45 /* 'E' */, 0x3B /* to pos 246 state 102 */,
   0x41 /* 'A' */, 0x84 /* to pos 394 state 175 */,
   0xCE /* 'N' */, 0x8A /* to pos 408 state 182 */,
/* pos 134: state 48 */
   0xE5 /* 'e' */, 0x01 /* to pos 136 state 49 */,
/* pos 136: state 49 */
   0xF9 /* 'y' */, 0x01 /* to pos 138 state 50 */,
/* pos 138: state 50 */
   0x31 /* '1' */, 0x03 /* to pos 144 state 51 */,
   0x32 /* '2' */, 0x04 /* to pos 148 state 53 */,
   0xBA /* ':' */, 0x24 /* to pos 214 state 86 */,
/* pos 144: state 51 */
   0xBA /* ':' */, 0x01 /* to pos 146 state 52 */,
/* pos 146: state 52 */
   0x85, 0x00 /* terminal marker */,
/* pos 148: state 53 */
   0xBA /* ':' */, 0x01 /* to pos 150 state 54 */,
/* pos 150: state 54 */
   0x86, 0x00 /* terminal marker */,
/* pos 152: state 55 */
   0xF2 /* 'r' */, 0x01 /* to pos 154 state 56 */,
/* pos 154: state 56 */
   0xEF /* 'o' */, 0x01 /* to pos 156 state 57 */,
/* pos 156: state 57 */
   0xF4 /* 't' */, 0x01 /* to pos 158 state 58 */,
/* pos 158: state 58 */
   0xEF /* 'o' */, 0x01 /* to pos 160 state 59 */,
/* pos 160: state 59 */
   0xE3 /* 'c' */, 0x01 /* to pos 162 state 60 */,
/* pos 162: state 60 */
   0xEF /* 'o' */, 0x01 /* to pos 164 state 61 */,
/* pos 164: state 61 */
   0xEC /* 'l' */, 0x01 /* to pos 166 state 62 */,
/* pos 166: state 62 */
   0xBA /* ':' */, 0x01 /* to pos 168 state 63 */,
/* pos 168: state 63 */
   0x87, 0x00 /* terminal marker */,
/* pos 170: state 64 */
   0xF0 /* 'p' */, 0x01 /* to pos 172 state 65 */,
/* pos 172: state 65 */
   0xE7 /* 'g' */, 0x01 /* to pos 174 state 66 */,
/* pos 174: state 66 */
   0xF2 /* 'r' */, 0x01 /* to pos 176 state 67 */,
/* pos 176: state 67 */
   0xE1 /* 'a' */, 0x01 /* to pos 178 state 68 */,
/* pos 178: state 68 */
   0xE4 /* 'd' */, 0x01 /* to pos 180 state 69 */,
/* pos 180: state 69 */
   0xE5 /* 'e' */, 0x01 /* to pos 182 state 70 */,
/* pos 182: state 70 */
   0xBA /* ':' */, 0x01 /* to pos 184 state 71 */,
/* pos 184: state 71 */
   0x88, 0x00 /* terminal marker */,
/* pos 186: state 72 */
   0xE9 /* 'i' */, 0x01 /* to pos 188 state 73 */,
/* pos 188: state 73 */
   0xE7 /* 'g' */, 0x01 /* to pos 190 state 74 */,
/* pos 190: state 74 */
   0xE9 /* 'i' */, 0x01 /* to pos 192 state 75 */,
/* pos 192: state 75 */
   0xEE /* 'n' */, 0x01 /* to pos 194 state 76 */,
/* pos 194: state 76 */
   0xBA /* ':' */, 0x01 /* to pos 196 state 77 */,
/* pos 196: state 77 */
   0x89, 0x00 /* terminal marker */,
/* pos 198: state 78 */
   0xF2 /* 'r' */, 0x01 /* to pos 200 state 79 */,
/* pos 200: state 79 */
   0xE1 /* 'a' */, 0x01 /* to pos 202 state 80 */,
/* pos 202: state 80 */
   0xE6 /* 'f' */, 0x01 /* to pos 204 state 81 */,
/* pos 204: state 81 */
   0xF4 /* 't' */, 0x01 /* to pos 206 state 82 */,
/* pos 206: state 82 */
   0xBA /* ':' */, 0x01 /* to pos 208 state 83 */,
/* pos 208: state 83 */
   0x8A, 0x00 /* terminal marker */,
/* pos 210: state 84 */
   0x8A /* '.' */, 0x01 /* to pos 212 state 85 */,
/* pos 212: state 85 */
   0x8B, 0x00 /* terminal marker */,
/* pos 214: state 86 */
   0x8C, 0x00 /* terminal marker */,
/* pos 216: state 87 */
   0xE5 /* 'e' */, 0x01 /* to pos 218 state 88 */,
/* pos 218: state 88 */
   0xF2 /* 'r' */, 0x01 /* to pos 220 state 89 */,
/* pos 220: state 89 */
   0xF3 /* 's' */, 0x01 /* to pos 222 state 90 */,
/* pos 222: state 90 */
   0xE9 /* 'i' */, 0x01 /* to pos 224 state 91 */,
/* pos 224: state 91 */
   0xEF /* 'o' */, 0x01 /* to pos 226 state 92 */,
/* pos 226: state 92 */
   0xEE /* 'n' */, 0x01 /* to pos 228 state 93 */,
/* pos 228: state 93 */
   0xBA /* ':' */, 0x01 /* to pos 230 state 94 */,
/* pos 230: state 94 */
   0x8D, 0x00 /* terminal marker */,
/* pos 232: state 95 */
   0xF2 /* 'r' */, 0x01 /* to pos 234 state 96 */,
/* pos 234: state 96 */
   0xE9 /* 'i' */, 0x01 /* to pos 236 state 97 */,
/* pos 236: state 97 */
   0xE7 /* 'g' */, 0x01 /* to pos 238 state 98 */,
/* pos 238: state 98 */
   0xE9 /* 'i' */, 0x01 /* to pos 240 state 99 */,
/* pos 240: state 99 */
   0xEE /* 'n' */, 0x01 /* to pos 242 state 100 */,
/* pos 242: state 100 */
   0xBA /* ':' */, 0x01 /* to pos 244 state 101 */,
/* pos 244: state 101 */
   0x8E, 0x00 /* terminal marker */,
/* pos 246: state 102 */
   0xF8 /* 'x' */, 0x01 /* to pos 248 state 103 */,
/* pos 248: state 103 */
   0xF4 /* 't' */, 0x01 /* to pos 250 state 104 */,
/* pos 250: state 104 */
   0xE5 /* 'e' */, 0x01 /* to pos 252 state 105 */,
/* pos 252: state 105 */
   0xEE /* 'n' */, 0x01 /* to pos 254 state 106 */,
/* pos 254: state 106 */
   0xF3 /* 's' */, 0x01 /* to pos 256 state 107 */,
/* pos 256: state 107 */
   0xE9 /* 'i' */, 0x01 /* to pos 258 state 108 */,
/* pos 258: state 108 */
   0xEF /* 'o' */, 0x01 /* to pos 260 state 109 */,
/* pos 260: state 109 */
   0xEE /* 'n' */, 0x01 /* to pos 262 state 110 */,
/* pos 262: state 110 */
   0xF3 /* 's' */, 0x01 /* to pos 264 state 111 */,
/* pos 264: state 111 */
   0xBA /* ':' */, 0x01 /* to pos 266 state 112 */,
/* pos 266: state 112 */
   0x8F, 0x00 /* terminal marker */,
/* pos 268: state 113 */
   0xE6 /* 'f' */, 0x01 /* to pos 270 state 114 */,
/* pos 270: state 114 */
   0xAD /* '-' */, 0x01 /* to pos 272 state 115 */,
/* pos 272: state 115 */
   0xCE /* 'N' */, 0x01 /* to pos 274 state 116 */,
/* pos 274: state 116 */
   0xEF /* 'o' */, 0x01 /* to pos 276 state 117 */,
/* pos 276: state 117 */
   0xEE /* 'n' */, 0x01 /* to pos 278 state 118 */,
/* pos 278: state 118 */
   0xE5 /* 'e' */, 0x01 /* to pos 280 state 119 */,
/* pos 280: state 119 */
   0xAD /* '-' */, 0x01 /* to pos 282 state 120 */,
/* pos 282: state 120 */
   0xCD /* 'M' */, 0x01 /* to pos 284 state 121 */,
/* pos 284: state 121 */
   0xE1 /* 'a' */, 0x01 /* to pos 286 state 122 */,
/* pos 286: state 122 */
   0xF4 /* 't' */, 0x01 /* to pos 288 state 123 */,
/* pos 288: state 123 */
   0xE3 /* 'c' */, 0x01 /* to pos 290 state 124 */,
/* pos 290: state 124 */
   0xE8 /* 'h' */, 0x01 /* to pos 292 state 125 */,
/* pos 292: state 125 */
   0xBA /* ':' */, 0x01 /* to pos 294 state 126 */,
/* pos 294: state 126 */
   0x90, 0x00 /* terminal marker */,
/* pos 296: state 127 */
   0xE5 /* 'e' */, 0x01 /* to pos 298 state 128 */,
/* pos 298: state 128 */
   0xEE /* 'n' */, 0x01 /* to pos 300 state 129 */,
/* pos 300: state 129 */
   0xF4 /* 't' */, 0x01 /* to pos 302 state 130 */,
/* pos 302: state 130 */
   0xAD /* '-' */, 0x01 /* to pos 304 state 131 */,
/* pos 304: state 131 */
   0x54 /* 'T' */, 0x02 /* to pos 308 state 132 */,
   0xCC /* 'L' */, 0x06 /* to pos 318 state 137 */,
/* pos 308: state 132 */
   0xF9 /* 'y' */, 0x01 /* to pos 310 state 133 */,
/* pos 310: state 133 */
   0xF0 /* 'p' */, 0x01 /* to pos 312 state 134 */,
/* pos 312: state 134 */
   0xE5 /* 'e' */, 0x01 /* to pos 314 state 135 */,
/* pos 314: state 135 */
   0xBA /* ':' */, 0x01 /* to pos 316 state 136 */,
/* pos 316: state 136 */
   0x91, 0x00 /* terminal marker */,
/* pos 318: state 137 */
   0xE5 /* 'e' */, 0x01 /* to pos 320 state 138 */,
/* pos 320: state 138 */
   0xEE /* 'n' */, 0x01 /* to pos 322 state 139 */,
/* pos 322: state 139 */
   0xE7 /* 'g' */, 0x01 /* to pos 324 state 140 */,
/* pos 324: state 140 */
   0xF4 /* 't' */, 0x01 /* to pos 326 state 141 */,
/* pos 326: state 141 */
   0xE8 /* 'h' */, 0x01 /* to pos 328 state 142 */,
/* pos 328: state 142 */
   0xBA /* ':' */, 0x01 /* to pos 330 state 143 */,
/* pos 330: state 143 */
   0x92, 0x00 /* terminal marker */,
/* pos 332: state 144 */
   0xE3 /* 'c' */, 0x01 /* to pos 334 state 145 */,
/* pos 334: state 145 */
   0xE3 /* 'c' */, 0x01 /* to pos 336 state 146 */,
/* pos 336: state 146 */
   0xE5 /* 'e' */, 0x01 /* to pos 338 state 147 */,
/* pos 338: state 147 */
   0xF3 /* 's' */, 0x01 /* to pos 340 state 148 */,
/* pos 340: state 148 */
   0xF3 /* 's' */, 0x01 /* to pos 342 state 149 */,
/* pos 342: state 149 */
   0xAD /* '-' */, 0x01 /* to pos 344 state 150 */,
/* pos 344: state 150 */
   0xC3 /* 'C' */, 0x01 /* to pos 346 state 151 */,
/* pos 346: state 151 */
   0xEF /* 'o' */, 0x01 /* to pos 348 state 152 */,
/* pos 348: state 152 */
   0xEE /* 'n' */, 0x01 /* to pos 350 state 153 */,
/* pos 350: state 153 */
   0xF4 /* 't' */, 0x01 /* to pos 352 state 154 */,
/* pos 352: state 154 */
   0xF2 /* 'r' */, 0x01 /* to pos 354 state 155 */,
/* pos 354: state 155 */
   0xEF /* 'o' */, 0x01 /* to pos 356 state 156 */,
/* pos 356: state 156 */
   0xEC /* 'l' */, 0x01 /* to pos 358 state 157 */,
/* pos 358: state 157 */
   0xAD /* '-' */, 0x01 /* to pos 360 state 158 */,
/* pos 360: state 158 */
   0xD2 /* 'R' */, 0x01 /* to pos 362 state 159 */,
/* pos 362: state 159 */
   0xE5 /* 'e' */, 0x01 /* to pos 364 state 160 */,
/* pos 364: state 160 */
   0xF1 /* 'q' */, 0x01 /* to pos 366 state 161 */,
/* pos 366: state 161 */
   0xF5 /* 'u' */, 0x01 /* to pos 368 state 162 */,
/* pos 368: state 162 */
   0xE5 /* 'e' */, 0x01 /* to pos 370 state 163 */,
/* pos 370: state 163 */
   0xF3 /* 's' */, 0x01 /* to pos 372 state 164 */,
/* pos 372: state 164 */
   0xF4 /* 't' */, 0x01 /* to pos 374 state 165 */,
/* pos 374: state 165 */
   0xAD /* '-' */, 0x01 /* to pos 376 state 166 */,
/* pos 376: state 166 */
   0xC8 /* 'H' */, 0x01 /* to pos 378 state 167 */,
/* pos 378: state 167 */
   0xE5 /* 'e' */, 0x01 /* to pos 380 state 168 */,
/* pos 380: state 168 */
   0xE1 /* 'a' */, 0x01 /* to pos 382 state 169 */,
/* pos 382: state 169 */
   0xE4 /* 'd' */, 0x01 /* to pos 384 state 170 */,
/* pos 384: state 170 */
   0xE5 /* 'e' */, 0x01 /* to pos 386 state 171 */,
/* pos 386: state 171 */
   0xF2 /* 'r' */, 0x01 /* to pos 388 state 172 */,
/* pos 388: state 172 */
   0xF3 /* 's' */, 0x01 /* to pos 390 state 173 */,
/* pos 390: state 173 */
   0xBA /* ':' */, 0x01 /* to pos 392 state 174 */,
/* pos 392: state 174 */
   0x93, 0x00 /* terminal marker */,
/* pos 394: state 175 */
   0xE3 /* 'c' */, 0x01 /* to pos 396 state 176 */,
/* pos 396: state 176 */
   0xE3 /* 'c' */, 0x01 /* to pos 398 state 177 */,
/* pos 398: state 177 */
   0xE5 /* 'e' */, 0x01 /* to pos 400 state 178 */,
/* pos 400: state 178 */
   0xF0 /* 'p' */, 0x01 /* to pos 402 state 179 */,
/* pos 402: state 179 */
   0xF4 /* 't' */, 0x01 /* to pos 404 state 180 */,
/* pos 404: state 180 */
   0xBA /* ':' */, 0x01 /* to pos 406 state 181 */,
/* pos 406: state 181 */
   0x94, 0x00 /* terminal marker */,
/* pos 408: state 182 */
   0xEF /* 'o' */, 0x01 /* to pos 410 state 183 */,
/* pos 410: state 183 */
   0xEE /* 'n' */, 0x01 /* to pos 412 state 184 */,
/* pos 412: state 184 */
   0xE3 /* 'c' */, 0x01 /* to pos 414 state 185 */,
/* pos 414: state 185 */
   0xE5 /* 'e' */, 0x01 /* to pos 416 state 186 */,
/* pos 416: state 186 */
   0xBA /* ':' */, 0x01 /* to pos 418 state 187 */,
/* pos 418: state 187 */
   0x95, 0x00 /* terminal marker */,
/* pos 420: state 188 */
   0xD4 /* 'T' */, 0x01 /* to pos 422 state 189 */,
/* pos 422: state 189 */
   0xD0 /* 'P' */, 0x01 /* to pos 424 state 190 */,
/* pos 424: state 190 */
   0xAF /* '/' */, 0x01 /* to pos 426 state 191 */,
/* pos 426: state 191 */
   0xB1 /* '1' */, 0x01 /* to pos 428 state 192 */,
/* pos 428: state 192 */
   0xAE /* '.' */, 0x01 /* to pos 430 state 193 */,
/* pos 430: state 193 */
   0xB1 /* '1' */, 0x01 /* to pos 432 state 194 */,
/* pos 432: state 194 */
   0xA0 /* ' ' */, 0x01 /* to pos 434 state 195 */,
/* pos 434: state 195 */
   0x96, 0x00 /* terminal marker */,
/* total size 436 bytes */
};

int lextable_decode(int pos, char c)
{
	while (pos >= 0) {
		if (lextable[pos + 1] == 0) /* terminal marker */
			return pos;

		if ((lextable[pos] & 0x7f) == c)
			return pos + (lextable[pos + 1] << 1);

		if (lextable[pos] & 0x80)
			return -1;

		pos += 2;
	}
	return pos;
}

int lws_allocate_header_table(struct libwebsocket *wsi)
{
	wsi->u.hdr.ah = malloc(sizeof(*wsi->u.hdr.ah));
	if (wsi->u.hdr.ah == NULL) {
		lwsl_err("Out of memory\n");
		return -1;
	}
	memset(wsi->u.hdr.ah->frag_index, 0, sizeof(wsi->u.hdr.ah->frag_index));
	wsi->u.hdr.ah->next_frag_index = 0;
	wsi->u.hdr.ah->pos = 0;

	return 0;
}

LWS_VISIBLE int lws_hdr_total_length(struct libwebsocket *wsi, enum lws_token_indexes h)
{
	int n;
	int len = 0;

	n = wsi->u.hdr.ah->frag_index[h];
	if (n == 0)
		return 0;

	do {
		len += wsi->u.hdr.ah->frags[n].len;
		n = wsi->u.hdr.ah->frags[n].next_frag_index;
	} while (n);

	return len;
}

LWS_VISIBLE int lws_hdr_copy(struct libwebsocket *wsi, char *dest, int len,
						enum lws_token_indexes h)
{
	int toklen = lws_hdr_total_length(wsi, h);
	int n;

	if (toklen >= len)
		return -1;

	n = wsi->u.hdr.ah->frag_index[h];
	if (n == 0)
		return 0;

	do {
		strcpy(dest,
			&wsi->u.hdr.ah->data[wsi->u.hdr.ah->frags[n].offset]);
		dest += wsi->u.hdr.ah->frags[n].len;
		n = wsi->u.hdr.ah->frags[n].next_frag_index;
	} while (n);

	return toklen;
}

char *lws_hdr_simple_ptr(struct libwebsocket *wsi, enum lws_token_indexes h)
{
	int n;

	n = wsi->u.hdr.ah->frag_index[h];
	if (!n)
		return NULL;

	return &wsi->u.hdr.ah->data[wsi->u.hdr.ah->frags[n].offset];
}

int lws_hdr_simple_create(struct libwebsocket *wsi,
				enum lws_token_indexes h, const char *s)
{
	wsi->u.hdr.ah->next_frag_index++;
	if (wsi->u.hdr.ah->next_frag_index ==
	       sizeof(wsi->u.hdr.ah->frags) / sizeof(wsi->u.hdr.ah->frags[0])) {
		lwsl_warn("More hdr frags than we can deal with, dropping\n");
		return -1;
	}

	wsi->u.hdr.ah->frag_index[h] = wsi->u.hdr.ah->next_frag_index;

	wsi->u.hdr.ah->frags[wsi->u.hdr.ah->next_frag_index].offset =
							     wsi->u.hdr.ah->pos;
	wsi->u.hdr.ah->frags[wsi->u.hdr.ah->next_frag_index].len = 0;
	wsi->u.hdr.ah->frags[wsi->u.hdr.ah->next_frag_index].next_frag_index =
									      0;

	do {
		if (wsi->u.hdr.ah->pos == sizeof(wsi->u.hdr.ah->data)) {
			lwsl_err("Ran out of header data space\n");
			return -1;
		}
		wsi->u.hdr.ah->data[wsi->u.hdr.ah->pos++] = *s;
		if (*s)
			wsi->u.hdr.ah->frags[
					wsi->u.hdr.ah->next_frag_index].len++;
	} while (*s++);

	return 0;
}


int libwebsockets_is_http_method(int parser_state)
{
    int is_method = 0;
    switch(parser_state) {
        case WSI_TOKEN_GET_URI:
        case WSI_TOKEN_POST_URI:
        case WSI_TOKEN_OPTIONS_URI:
            is_method = 1;
            break;
        default:
            break;
    };
    return is_method;
};

/* TODO: Find out why this is necessary.
 * I think it may be caused by multiple requests being made over a keep-alive
 * session. */
#define LWS_PARSE_REALLOCATE_HEADERS

int libwebsocket_parse(struct libwebsocket *wsi, unsigned char c)
{
#ifdef LWS_PARSE_REALLOCATE_HEADERS
    if( wsi && !wsi->u.hdr.ah ) {
        /* Don't warn on this. We do this for repeat requests on a persistent
         * connection.*/
        /*
        lwsl_warn("Allocating headers for %i (%p) again...\n",
                wsi->sock, (void*)wsi);
        */
        lws_allocate_header_table(wsi);
    };
#endif

	int n;
	switch (wsi->u.hdr.parser_state) {
	case WSI_TOKEN_GET_URI:
	case WSI_TOKEN_POST_URI:
	case WSI_TOKEN_OPTIONS_URI:
	case WSI_TOKEN_HOST:
	case WSI_TOKEN_CONNECTION:
	case WSI_TOKEN_KEY1:
	case WSI_TOKEN_KEY2:
	case WSI_TOKEN_PROTOCOL:
	case WSI_TOKEN_UPGRADE:
	case WSI_TOKEN_ORIGIN:
	case WSI_TOKEN_SWORIGIN:
	case WSI_TOKEN_DRAFT:
	case WSI_TOKEN_CHALLENGE:
	case WSI_TOKEN_KEY:
	case WSI_TOKEN_VERSION:
	case WSI_TOKEN_ACCEPT:
	case WSI_TOKEN_NONCE:
	case WSI_TOKEN_EXTENSIONS:
    case WSI_TOKEN_ETAG_MATCH:
    case WSI_TOKEN_CONTENT_TYPE:
    case WSI_TOKEN_CONTENT_LENGTH:
    case WSI_TOKEN_AC_REQUEST_HEADERS:
	case WSI_TOKEN_HTTP:
    case WSI_TOKEN_HTTP_VERSION:
        {
        int parse_content_length = 0;
		lwsl_parser("WSI_TOK_(%d) '%c'\n", wsi->u.hdr.parser_state, c);

		/* collect into malloc'd buffers */
		/* optional initial space swallow */
		if (!wsi->u.hdr.ah->frags[wsi->u.hdr.ah->frag_index[
				      wsi->u.hdr.parser_state]].len && c == ' ')
			break;

		/* special case space terminator for get-uri */
		if (libwebsockets_is_http_method(wsi->u.hdr.parser_state) && c == ' ') {
			c = '\0';
            wsi->u.hdr.http_method = wsi->u.hdr.http_method;
			wsi->u.hdr.parser_state = WSI_TOKEN_HTTP_VERSION;
            goto start_fragment;
		}

		/* bail at EOL */
		if (wsi->u.hdr.parser_state != WSI_TOKEN_CHALLENGE &&
								  c == '\x0d') {
			c = '\0';
            if( wsi->u.hdr.parser_state == WSI_TOKEN_CONTENT_LENGTH) {
                parse_content_length = 1;
            };
			wsi->u.hdr.parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
			lwsl_parser("*\n");

		}

		if (wsi->u.hdr.ah->pos == sizeof(wsi->u.hdr.ah->data)) {
			lwsl_warn("excessive header content\n");
			return -1;
		}
		wsi->u.hdr.ah->data[wsi->u.hdr.ah->pos++] = c;
		if (c)
			wsi->u.hdr.ah->frags[
					wsi->u.hdr.ah->next_frag_index].len++;

        if (parse_content_length) {
            int token_len=0;
            char content_len_buf[10];
            token_len = lws_hdr_copy(wsi, content_len_buf, 10, WSI_TOKEN_CONTENT_LENGTH);
            content_len_buf[token_len] = '\0';
            wsi->u.hdr.content_length = atoi(content_len_buf);
            lwsl_parser("Content length: %li\n", wsi->u.hdr.content_length);
        };

		/* per-protocol end of headers management */

		if (wsi->u.hdr.parser_state == WSI_TOKEN_CHALLENGE) {
			goto set_parsing_complete;
        };
		break;
        }

		/* collecting and checking a name part */
	case WSI_TOKEN_NAME_PART:
		lwsl_parser("WSI_TOKEN_NAME_PART '%c'\n", c);

		wsi->u.hdr.lextable_pos =
				lextable_decode(wsi->u.hdr.lextable_pos, c);

		if (wsi->u.hdr.lextable_pos < 0) {
			/* this is not a header we know about */
			if (wsi->u.hdr.ah->frag_index[WSI_TOKEN_REQUEST_HTTP_METHOD] ||
				    wsi->u.hdr.ah->frag_index[WSI_TOKEN_REQUEST_HTTP_METHOD]) {
				/*
				 * altready had the method, no idea what
				 * this crap is, ignore
				 */
				wsi->u.hdr.parser_state = WSI_TOKEN_SKIPPING;
				break;
			}
			/*
			 * hm it's an unknown http method in fact,
			 * treat as dangerous
			 */

			lwsl_parser("Unknown method - dropping\n");
			return -1;
		}
		if (lextable[wsi->u.hdr.lextable_pos + 1] == 0) {

			/* terminal state */

			n = lextable[wsi->u.hdr.lextable_pos] & 0x7f;

			lwsl_parser("known hdr %d\n", n);
            switch( n )
            {
                case WSI_TOKEN_GET_URI:
                case WSI_TOKEN_POST_URI:
                case WSI_TOKEN_OPTIONS_URI:
                    wsi->u.hdr.have_method = 1;
                    wsi->u.hdr.http_method = n;
                    break;

                case WSI_TOKEN_CONTENT_LENGTH:
                    wsi->u.hdr.have_body = 1;
                    break;
                /*
                 * WSORIGIN is protocol equiv to ORIGIN,
                 * JWebSocket likes to send it, map to ORIGIN
                 */
                case WSI_TOKEN_SWORIGIN:
				    n = WSI_TOKEN_ORIGIN;
                    break;
            };

            if (n == WSI_TOKEN_REQUEST_HTTP_METHOD && wsi->u.hdr.ah &&
				wsi->u.hdr.ah->frag_index[WSI_TOKEN_REQUEST_HTTP_METHOD]) {
				lwsl_warn("Duplicated method\n");
				return -1;
			}

			wsi->u.hdr.parser_state = (enum lws_token_indexes)
							(WSI_TOKEN_GET_URI + n);

            /* Got to complete, or start looking for body data: */
			if (wsi->u.hdr.parser_state == WSI_TOKEN_CHALLENGE) {
                if (wsi->u.hdr.have_body && wsi->u.hdr.content_length > 0) {
                    wsi->body_index = 0;
                    wsi->u.hdr.parser_state = WSI_TOKEN_BODY;
                    lwsl_parser("Saw challenge. Looking for body.\n");
                    break;
                }
                else {
				    goto set_parsing_complete;
                };
            };
			goto start_fragment;
		}
		break;

start_fragment:
		wsi->u.hdr.ah->next_frag_index++;
		if (wsi->u.hdr.ah->next_frag_index ==
				sizeof(wsi->u.hdr.ah->frags) /
					      sizeof(wsi->u.hdr.ah->frags[0])) {
			lwsl_warn("More hdr frags than we can deal with\n");
			return -1;
		}

		wsi->u.hdr.ah->frags[wsi->u.hdr.ah->next_frag_index].offset =
							     wsi->u.hdr.ah->pos;
		wsi->u.hdr.ah->frags[wsi->u.hdr.ah->next_frag_index].len = 0;
		wsi->u.hdr.ah->frags[
			    wsi->u.hdr.ah->next_frag_index].next_frag_index = 0;

		n = wsi->u.hdr.ah->frag_index[wsi->u.hdr.parser_state];
		if (!n) { /* first fragment */
			wsi->u.hdr.ah->frag_index[wsi->u.hdr.parser_state] =
						 wsi->u.hdr.ah->next_frag_index;
		} else { /* continuation */
			while (wsi->u.hdr.ah->frags[n].next_frag_index)
				n = wsi->u.hdr.ah->frags[n].next_frag_index;
			wsi->u.hdr.ah->frags[n].next_frag_index =
						 wsi->u.hdr.ah->next_frag_index;

			if (wsi->u.hdr.ah->pos == sizeof(wsi->u.hdr.ah->data)) {
				lwsl_warn("excessive header content\n");
				return -1;
			}

			wsi->u.hdr.ah->data[wsi->u.hdr.ah->pos++] = ' ';
			wsi->u.hdr.ah->frags[
					  wsi->u.hdr.ah->next_frag_index].len++;
		}

		break;


		/* skipping arg part of a name we didn't recognize */
	case WSI_TOKEN_SKIPPING:
		lwsl_parser("WSI_TOKEN_SKIPPING '%c'\n", c);
		if (c == '\x0d')
			wsi->u.hdr.parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
		break;

	case WSI_TOKEN_SKIPPING_SAW_CR:
		lwsl_parser("WSI_TOKEN_SKIPPING_SAW_CR '%c'\n", c);
		if (c == '\x0a') {
			wsi->u.hdr.parser_state = WSI_TOKEN_NAME_PART;
			wsi->u.hdr.lextable_pos = 0;
		} else
			wsi->u.hdr.parser_state = WSI_TOKEN_SKIPPING;
		break;
    case WSI_TOKEN_BODY:
        /* append to the body buffer, if we have space: */
        if( wsi->body_index < LWS_MAX_BODY_LEN -2) {
            lwsl_parser("WSI_TOKEN_BODY: '%c' (STORING)\n", c);
            wsi->body[wsi->body_index++] = c;
            wsi->body[wsi->body_index] = '\0';
        };

        /* If we've consumed what the client sent, be done: */
        if( wsi->body_index >= wsi->u.hdr.content_length ) {
            lwsl_parser("WSI_TOKEN_BODY: '%c' (OUT OF SPACE)\n", c);
            goto set_parsing_complete;
        };
        break;
		/* we're done, ignore anything else */
	case WSI_PARSING_COMPLETE:
		lwsl_parser("WSI_PARSING_COMPLETE '%c'\n", c);
		break;

	default:	/* keep gcc happy */
		break;
	}

	return 0;

set_parsing_complete:

	if (lws_hdr_total_length(wsi, WSI_TOKEN_UPGRADE)) {
		if (lws_hdr_total_length(wsi, WSI_TOKEN_VERSION))
			wsi->ietf_spec_revision =
			       atoi(lws_hdr_simple_ptr(wsi, WSI_TOKEN_VERSION));

		lwsl_parser("v%02d hdrs completed\n", wsi->ietf_spec_revision);
	}
	wsi->u.hdr.parser_state = WSI_PARSING_COMPLETE;
	wsi->hdr_parsing_completed = 1;

	return 0;
}


/**
 * lws_frame_is_binary: true if the current frame was sent in binary mode
 *
 * @wsi: the connection we are inquiring about
 *
 * This is intended to be called from the LWS_CALLBACK_RECEIVE callback if
 * it's interested to see if the frame it's dealing with was sent in binary
 * mode.
 */

LWS_VISIBLE int lws_frame_is_binary(struct libwebsocket *wsi)
{
	return wsi->u.ws.frame_is_binary;
}

int
libwebsocket_rx_sm(struct libwebsocket *wsi, unsigned char c)
{
	int n;
	struct lws_tokens eff_buf;
	int ret = 0;
#ifndef LWS_NO_EXTENSIONS
	int handled;
	int m;
#endif

#if 0
	lwsl_debug("RX: %02X ", c);
#endif

	switch (wsi->lws_rx_parse_state) {
	case LWS_RXPS_NEW:

		switch (wsi->ietf_spec_revision) {
		case 13:
			/*
			 * no prepended frame key any more
			 */
			wsi->u.ws.all_zero_nonce = 1;
			goto handle_first;

		default:
			lwsl_warn("lws_rx_sm: unknown spec version %d\n",
						       wsi->ietf_spec_revision);
			break;
		}
		break;
	case LWS_RXPS_04_MASK_NONCE_1:
		wsi->u.ws.frame_masking_nonce_04[1] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_2;
		break;
	case LWS_RXPS_04_MASK_NONCE_2:
		wsi->u.ws.frame_masking_nonce_04[2] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_3;
		break;
	case LWS_RXPS_04_MASK_NONCE_3:
		wsi->u.ws.frame_masking_nonce_04[3] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;

		/*
		 * start from the zero'th byte in the XOR key buffer since
		 * this is the start of a frame with a new key
		 */

		wsi->u.ws.frame_mask_index = 0;

		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_1;
		break;

	/*
	 *  04 logical framing from the spec (all this is masked when incoming
	 *  and has to be unmasked)
	 *
	 * We ignore the possibility of extension data because we don't
	 * negotiate any extensions at the moment.
	 *
	 *    0                   1                   2                   3
	 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *   +-+-+-+-+-------+-+-------------+-------------------------------+
	 *   |F|R|R|R| opcode|R| Payload len |    Extended payload length    |
	 *   |I|S|S|S|  (4)  |S|     (7)     |             (16/63)           |
	 *   |N|V|V|V|       |V|             |   (if payload len==126/127)   |
	 *   | |1|2|3|       |4|             |                               |
	 *   +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	 *   |     Extended payload length continued, if payload len == 127  |
	 *   + - - - - - - - - - - - - - - - +-------------------------------+
	 *   |                               |         Extension data        |
	 *   +-------------------------------+ - - - - - - - - - - - - - - - +
	 *   :                                                               :
	 *   +---------------------------------------------------------------+
	 *   :                       Application data                        :
	 *   +---------------------------------------------------------------+
	 *
	 *  We pass payload through to userland as soon as we get it, ignoring
	 *  FIN.  It's up to userland to buffer it up if it wants to see a
	 *  whole unfragmented block of the original size (which may be up to
	 *  2^63 long!)
	 */

	case LWS_RXPS_04_FRAME_HDR_1:
handle_first:

		wsi->u.ws.opcode = c & 0xf;
		wsi->u.ws.rsv = c & 0x70;
		wsi->u.ws.final = !!((c >> 7) & 1);

		switch (wsi->u.ws.opcode) {
		case LWS_WS_OPCODE_07__TEXT_FRAME:
		case LWS_WS_OPCODE_07__BINARY_FRAME:
			wsi->u.ws.frame_is_binary =
			     wsi->u.ws.opcode == LWS_WS_OPCODE_07__BINARY_FRAME;
			break;
		}
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN:

		wsi->u.ws.this_frame_masked = !!(c & 0x80);

		switch (c & 0x7f) {
		case 126:
			/* control frames are not allowed to have big lengths */
			if (wsi->u.ws.opcode & 8)
				goto illegal_ctl_length;

			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_2;
			break;
		case 127:
			/* control frames are not allowed to have big lengths */
			if (wsi->u.ws.opcode & 8)
				goto illegal_ctl_length;

			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_8;
			break;
		default:
			wsi->u.ws.rx_packet_length = c & 0x7f;
			if (wsi->u.ws.this_frame_masked)
				wsi->lws_rx_parse_state =
						LWS_RXPS_07_COLLECT_FRAME_KEY_1;
			else
				wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
			break;
		}
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_2:
		wsi->u.ws.rx_packet_length = c << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_1:
		wsi->u.ws.rx_packet_length |= c;
		if (wsi->u.ws.this_frame_masked)
			wsi->lws_rx_parse_state =
					LWS_RXPS_07_COLLECT_FRAME_KEY_1;
		else
			wsi->lws_rx_parse_state =
				LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_8:
		if (c & 0x80) {
			lwsl_warn("b63 of length must be zero\n");
			/* kill the connection */
			return -1;
		}
#if defined __LP64__
		wsi->u.ws.rx_packet_length = ((size_t)c) << 56;
#else
		wsi->u.ws.rx_packet_length = 0;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_7;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_7:
#if defined __LP64__
		wsi->u.ws.rx_packet_length |= ((size_t)c) << 48;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_6;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_6:
#if defined __LP64__
		wsi->u.ws.rx_packet_length |= ((size_t)c) << 40;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_5;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_5:
#if defined __LP64__
		wsi->u.ws.rx_packet_length |= ((size_t)c) << 32;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_4;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_4:
		wsi->u.ws.rx_packet_length |= ((size_t)c) << 24;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_3;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_3:
		wsi->u.ws.rx_packet_length |= ((size_t)c) << 16;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_2;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_2:
		wsi->u.ws.rx_packet_length |= ((size_t)c) << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_1:
		wsi->u.ws.rx_packet_length |= ((size_t)c);
		if (wsi->u.ws.this_frame_masked)
			wsi->lws_rx_parse_state =
					LWS_RXPS_07_COLLECT_FRAME_KEY_1;
		else
			wsi->lws_rx_parse_state =
				LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_1:
		wsi->u.ws.frame_masking_nonce_04[0] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_07_COLLECT_FRAME_KEY_2;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_2:
		wsi->u.ws.frame_masking_nonce_04[1] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_07_COLLECT_FRAME_KEY_3;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_3:
		wsi->u.ws.frame_masking_nonce_04[2] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_07_COLLECT_FRAME_KEY_4;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_4:
		wsi->u.ws.frame_masking_nonce_04[3] = c;
		if (c)
			wsi->u.ws.all_zero_nonce = 0;
		wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		wsi->u.ws.frame_mask_index = 0;
		if (wsi->u.ws.rx_packet_length == 0)
			goto spill;
		break;


	case LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED:

		if (!wsi->u.ws.rx_user_buffer)
			lwsl_err("NULL user buffer...\n");

		if (wsi->u.ws.all_zero_nonce)
			wsi->u.ws.rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
			       (wsi->u.ws.rx_user_buffer_head++)] = c;
		else
			wsi->u.ws.rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
			       (wsi->u.ws.rx_user_buffer_head++)] =
				   c ^ wsi->u.ws.frame_masking_nonce_04[
					    (wsi->u.ws.frame_mask_index++) & 3];

		if (--wsi->u.ws.rx_packet_length == 0) {
			/* spill because we have the whole frame */
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto spill;
		}

		/*
		 * if there's no protocol max frame size given, we are
		 * supposed to default to LWS_MAX_SOCKET_IO_BUF
		 */

		if (!wsi->protocol->rx_buffer_size &&
					wsi->u.ws.rx_user_buffer_head !=
							  LWS_MAX_SOCKET_IO_BUF)
			break;
		else
			if (wsi->protocol->rx_buffer_size &&
					wsi->u.ws.rx_user_buffer_head !=
						  wsi->protocol->rx_buffer_size)
			break;

		/* spill because we filled our rx buffer */
spill:
		/*
		 * is this frame a control packet we should take care of at this
		 * layer?  If so service it and hide it from the user callback
		 */

		lwsl_parser("spill on %s\n", wsi->protocol->name);

		switch (wsi->u.ws.opcode) {
		case LWS_WS_OPCODE_07__CLOSE:
			/* is this an acknowledgement of our close? */
			if (wsi->state == WSI_STATE_AWAITING_CLOSE_ACK) {
				/*
				 * fine he has told us he is closing too, let's
				 * finish our close
				 */
				lwsl_parser("seen client close ack\n");
				return -1;
			}
			lwsl_parser("server sees client close packet\n");
			/* parrot the close packet payload back */
			n = libwebsocket_write(wsi, (unsigned char *)
				&wsi->u.ws.rx_user_buffer[
					LWS_SEND_BUFFER_PRE_PADDING],
					wsi->u.ws.rx_user_buffer_head,
							       LWS_WRITE_CLOSE);
			if (n < 0)
				lwsl_info("write of close ack failed %d\n", n);
			wsi->state = WSI_STATE_RETURNED_CLOSE_ALREADY;
			/* close the connection */
			return -1;

		case LWS_WS_OPCODE_07__PING:
			lwsl_info("received %d byte ping, sending pong\n",
						 wsi->u.ws.rx_user_buffer_head);
			lwsl_hexdump(&wsi->u.ws.rx_user_buffer[
					LWS_SEND_BUFFER_PRE_PADDING],
						 wsi->u.ws.rx_user_buffer_head);
			/* parrot the ping packet payload back as a pong */
			n = libwebsocket_write(wsi, (unsigned char *)
			&wsi->u.ws.rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
				 wsi->u.ws.rx_user_buffer_head, LWS_WRITE_PONG);
			if (n < 0)
				return -1;
			/* ... then just drop it */
			wsi->u.ws.rx_user_buffer_head = 0;
			return 0;

		case LWS_WS_OPCODE_07__PONG:
			/* ... then just drop it */
			wsi->u.ws.rx_user_buffer_head = 0;
			return 0;

		case LWS_WS_OPCODE_07__TEXT_FRAME:
		case LWS_WS_OPCODE_07__BINARY_FRAME:
		case LWS_WS_OPCODE_07__CONTINUATION:
			break;

		default:
#ifndef LWS_NO_EXTENSIONS
			lwsl_parser("passing opc %x up to exts\n",
							wsi->u.ws.opcode);

			/*
			 * It's something special we can't understand here.
			 * Pass the payload up to the extension's parsing
			 * state machine.
			 */

			eff_buf.token = &wsi->u.ws.rx_user_buffer[
						   LWS_SEND_BUFFER_PRE_PADDING];
			eff_buf.token_len = wsi->u.ws.rx_user_buffer_head;

			handled = 0;
			for (n = 0; n < wsi->count_active_extensions; n++) {
				m = wsi->active_extensions[n]->callback(
					wsi->protocol->owning_server,
					wsi->active_extensions[n], wsi,
					LWS_EXT_CALLBACK_EXTENDED_PAYLOAD_RX,
					    wsi->active_extensions_user[n],
								   &eff_buf, 0);
				if (m)
					handled = 1;
			}

			if (!handled)
#endif
				lwsl_ext("ext opc opcode 0x%x unknown\n",
							      wsi->u.ws.opcode);

			wsi->u.ws.rx_user_buffer_head = 0;
			return 0;
		}

		/*
		 * No it's real payload, pass it up to the user callback.
		 * It's nicely buffered with the pre-padding taken care of
		 * so it can be sent straight out again using libwebsocket_write
		 */

		eff_buf.token = &wsi->u.ws.rx_user_buffer[
						LWS_SEND_BUFFER_PRE_PADDING];
		eff_buf.token_len = wsi->u.ws.rx_user_buffer_head;
#ifndef LWS_NO_EXTENSIONS
		for (n = 0; n < wsi->count_active_extensions; n++) {
			m = wsi->active_extensions[n]->callback(
				wsi->protocol->owning_server,
				wsi->active_extensions[n], wsi,
				LWS_EXT_CALLBACK_PAYLOAD_RX,
				wsi->active_extensions_user[n],
				&eff_buf, 0);
			if (m < 0) {
				lwsl_ext(
				 "Extension '%s' failed to handle payload!\n",
					      wsi->active_extensions[n]->name);
				return -1;
			}
		}
#endif
		if (eff_buf.token_len > 0) {
			eff_buf.token[eff_buf.token_len] = '\0';

			if (wsi->protocol->callback)
				ret = user_callback_handle_rxflow(
						wsi->protocol->callback,
						wsi->protocol->owning_server,
						wsi, LWS_CALLBACK_RECEIVE,
						wsi->user_space,
						eff_buf.token,
						eff_buf.token_len);
		    else
			    lwsl_err("No callback on payload spill!\n");
		}

		wsi->u.ws.rx_user_buffer_head = 0;
		break;
	}

	return ret;

illegal_ctl_length:

	lwsl_warn("Control frame with xtended length is illegal\n");
	/* kill the connection */
	return -1;
}


int libwebsocket_interpret_incoming_packet(struct libwebsocket *wsi,
						 unsigned char *buf, size_t len)
{
	size_t n = 0;
	int m;

#if 0
	lwsl_parser("received %d byte packet\n", (int)len);
	lwsl_hexdump(buf, len);
#endif

	/* let the rx protocol state machine have as much as it needs */

	while (n < len) {
		/*
		 * we were accepting input but now we stopped doing so
		 */
		if (!(wsi->u.ws.rxflow_change_to & LWS_RXFLOW_ALLOW)) {
			/* his RX is flowcontrolled, don't send remaining now */
			if (!wsi->u.ws.rxflow_buffer) {
				/* a new rxflow, buffer it and warn caller */
				lwsl_info("new rxflow input buffer len %d\n",
								       len - n);
				wsi->u.ws.rxflow_buffer =
					       (unsigned char *)malloc(len - n);
				wsi->u.ws.rxflow_len = len - n;
				wsi->u.ws.rxflow_pos = 0;
				memcpy(wsi->u.ws.rxflow_buffer,
							buf + n, len - n);
			} else
				/* rxflow while we were spilling prev rxflow */
				lwsl_info("stalling in existing rxflow buf\n");

			return 1;
		}

		/* account for what we're using in rxflow buffer */
		if (wsi->u.ws.rxflow_buffer)
			wsi->u.ws.rxflow_pos++;

		/* process the byte */
		m = libwebsocket_rx_sm(wsi, buf[n++]);
		if (m < 0)
			return -1;
	}

	return 0;
}


/**
 * libwebsockets_remaining_packet_payload() - Bytes to come before "overall"
 *					      rx packet is complete
 * @wsi:		Websocket instance (available from user callback)
 *
 *	This function is intended to be called from the callback if the
 *  user code is interested in "complete packets" from the client.
 *  libwebsockets just passes through payload as it comes and issues a buffer
 *  additionally when it hits a built-in limit.  The LWS_CALLBACK_RECEIVE
 *  callback handler can use this API to find out if the buffer it has just
 *  been given is the last piece of a "complete packet" from the client --
 *  when that is the case libwebsockets_remaining_packet_payload() will return
 *  0.
 *
 *  Many protocols won't care becuse their packets are always small.
 */

LWS_VISIBLE size_t
libwebsockets_remaining_packet_payload(struct libwebsocket *wsi)
{
	return wsi->u.ws.rx_packet_length;
}
