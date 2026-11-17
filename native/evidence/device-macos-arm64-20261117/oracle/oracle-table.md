| Scene | CPU vs Metal: practical / strict / max abs | CPU vs MetalRT: practical / strict / max abs | Metal vs MetalRT max abs | Metal repeat max abs | non-finite |
| --- | --- | --- | --- | --- | --- |
| `bsdf/glass-multiscatter-caustics-off` | 0 / 427 / 9.72e-06 | 0 / 440 / 1.34e-05 | 3.7e-06 | 5.96e-08 | 0 |
| `bsdf/glass-multiscatter-extremes` | 0 / 574 / 1.01e-05 | 0 / 554 / 1e-05 | 1.74e-06 | 5.96e-08 | 0 |
| `cameras/border-adaptive` | 0 / 328 / 5.38e-05 | 0 / 325 / 6.5e-06 | 4.73e-05 | 1.49e-07 | 0 |
| `curves/bump-strong-noise-generated` | 1425 / 3735 / 0.0316 | 2264 / 4143 / 0.0542 | 0.0644 | 1.19e-07 | 0 |
| `lamps/sphere-light-inside` | 0 / 276 / 8.79e-06 | 0 / 278 / 8.82e-06 | 5.3e-06 | 8.94e-08 | 0 |
| `mnee/glass-multiscatter-caustic` | 0 / 37 / 5.36e-06 | 0 / 38 / 7.51e-06 | 4.74e-06 | 5.96e-08 | 0 |
| `surface-nodes/bump-emission` | 2 / 571 / 8.58e-05 | 2 / 578 / 8.24e-05 | 1.63e-05 | 4.77e-07 | 0 |
| `surface-nodes/bump-emission-displacement` | 1 / 602 / 0.00288 | 2 / 617 / 0.00942 | 0.00942 | 4.77e-07 | 0 |
| `surface-nodes/bump-emission-lamp` | 0 / 156 / 1.22e-06 | 0 / 155 / 1.22e-06 | 5.96e-08 | 1.19e-07 | 0 |
| `surface-nodes/bump-emission-unsampled` | 1 / 769 / 8.58e-05 | 1 / 774 / 8.23e-05 | 1.85e-05 | 9.54e-07 | 0 |
| `surface-nodes/bump-emission-world` | 0 / 498 / 5.72e-06 | 0 / 498 / 5.72e-06 | 1.19e-07 | 1.19e-07 | 0 |
| `surface-nodes/bump-strong-noise-normal` | 38 / 1500 / 0.00632 | 39 / 1508 / 0.00632 | 0.00099 | 1.19e-07 | 0 |
| `surface-nodes/bump-volume` | 0 / 365 / 1.91e-06 | 0 / 373 / 1.91e-06 | 8.94e-08 | 5.96e-08 | 0 |
| `surface-nodes/raycast-ao-bevel-subsurface-volume` | 2 / 318 / 0.000145 | 5 / 322 / 0.002 | 0.002 | 1.19e-07 | 0 |
| `surface-nodes/tangent-node-emission` | 105 / 521 / 0.125 | 111 / 527 / 0.125 | 0.136 | 1.19e-07 | 0 |
| `surface-nodes/tangent-node-glossy-glass-normal` | 57 / 441 / 0.0406 | 79 / 456 / 0.0415 | 0.0209 | 5.96e-08 | 0 |
| `volumes/heterogeneous-open-boxes` | 497 / 537 / 0.212 | 501 / 537 / 0.212 | 0.122 | 5.96e-08 | 0 |
| `volumes/light-path-heterogeneous-emission` | 354 / 999 / 1.42 | 264 / 1011 / 0.915 | 1.42 | 1.91e-06 | 0 |

repoCommit: ['3f162bfe12a1d23188f087c4d9b5f8947995aa29']  repoWorkingTreeClean: ['True']
meta.json files with warnings: 0
