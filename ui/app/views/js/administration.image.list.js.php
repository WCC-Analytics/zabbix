<?php declare(strict_types=1);
/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


/**
 * @var CView $this
 */
?>

<script>
	$(() => {
		$('#imagetype').on('change', (e) => redirect(e.target.value));

		new orderLoadImages(document.querySelector('.adm-img'));
	});

	class orderLoadImages {

		constructor(root) {
			if (root instanceof Element) {
				this.loadImages(root);
			}
		}

		async loadImages(root) {
			const images = root.querySelectorAll('img[data-src]');

			if (images.length == 0) {
				return;
			}

			for (let i = 0; i < images.length; i++) {
				await this.loadImage(images[i]);
			}
		}

		loadImage(elem) {
			return new Promise((resolve, reject) => {
				elem.onload = () => {
					elem.removeAttribute('data-src');
					resolve(elem);
				};
				elem.onerror = reject;
				elem.src = elem.dataset.src;
			});
		}
	}
</script>
