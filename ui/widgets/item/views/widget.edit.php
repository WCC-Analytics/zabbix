<?php declare(strict_types = 0);
/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**/


/**
 * Item value widget form view.
 *
 * @var CView $this
 * @var array $data
 */

use Zabbix\Widgets\Fields\CWidgetFieldColumnsList;

$form = new CWidgetFormView($data);

$form
	->addField(
		(new CWidgetFieldMultiSelectItemView($data['fields']['itemid'], $data['captions']['ms']['items']['itemid']))
			->setPopupParameter('value_types', [
				ITEM_VALUE_TYPE_FLOAT,
				ITEM_VALUE_TYPE_STR,
				ITEM_VALUE_TYPE_LOG,
				ITEM_VALUE_TYPE_UINT64,
				ITEM_VALUE_TYPE_TEXT
			])
	)
	->addField(
		(new CWidgetFieldCheckBoxListView($data['fields']['show']))->setColumns(2)
	)
	->addFieldset(
		(new CWidgetFormFieldsetCollapsibleView(_('Advanced configuration')))
			->addFieldsGroup(
				getDescriptionFieldsGroupViews($form, $data['fields'])
			)
			->addFieldsGroup(
				getValueFieldsGroupViews($form, $data['fields'])
			)
			->addFieldsGroup(
				getTimeFieldsGroupViews($form, $data['fields'])
			)
			->addFieldsGroup(
				getChangeIndicatorFieldsGroupViews($data['fields'])
			)
			->addField(
				new CWidgetFieldColorView($data['fields']['bg_color'])
			)
			->addField(
				(new CWidgetFieldThresholdsView($data['fields']['thresholds']))->setHint(
					makeWarningIcon(_('This setting applies only to numeric data.'))
						->setId('item-value-thresholds-warning')
				)
			)
	)
	->addField(array_key_exists('dynamic', $data['fields'])
		? new CWidgetFieldCheckBoxView($data['fields']['dynamic'])
		: null
	)
	->includeJsFile('widget.edit.js.php')
	->addJavaScript('widget_item_form.init('.json_encode([
		'thresholds_colors' => CWidgetFieldColumnsList::THRESHOLDS_DEFAULT_COLOR_PALETTE
	], JSON_THROW_ON_ERROR).');')
	->show();

function getDescriptionFieldsGroupViews(CWidgetFormView $form, array $fields): CWidgetFieldsGroupView {
	$desc_size = $form->registerField(new CWidgetFieldIntegerBoxView($fields['desc_size']));

	return (new CWidgetFieldsGroupView(_('Description')))
		->setHelpHint([
			_('Supported macros:'),
			(new CList([
				'{HOST.*}',
				'{ITEM.*}',
				'{INVENTORY.*}',
				_('User macros')
			]))->addClass(ZBX_STYLE_LIST_DASHED)
		])
		->addField(
			(new CWidgetFieldTextAreaView($fields['description']))
				->setAdaptiveWidth(ZBX_TEXTAREA_BIG_WIDTH - 30)
				->removeLabel()
		)
		->addField(
			new CWidgetFieldRadioButtonListView($fields['desc_h_pos'])
		)
		->addItem([
			$desc_size->getLabel(),
			(new CFormField([$desc_size->getView(), '%']))->addClass('field-size')
		])
		->addField(
			new CWidgetFieldRadioButtonListView($fields['desc_v_pos'])
		)
		->addField(
			new CWidgetFieldCheckBoxView($fields['desc_bold'])
		)
		->addField(
			(new CWidgetFieldColorView($fields['desc_color']))->addLabelClass('offset-3')
		)
		->addRowClass('fields-group-description');
}

function getValueFieldsGroupViews(CWidgetFormView $form, array $fields): CWidgetFieldsGroupView {
	$decimal_size = $form->registerField(new CWidgetFieldIntegerBoxView($fields['decimal_size']));
	$value_size = $form->registerField(new CWidgetFieldIntegerBoxView($fields['value_size']));
	$units_show = $form->registerField(new CWidgetFieldCheckBoxView($fields['units_show']));
	$units = $form->registerField(
		(new CWidgetFieldTextBoxView($fields['units']))->setAdaptiveWidth(ZBX_TEXTAREA_BIG_WIDTH)
	);
	$units_size = $form->registerField(new CWidgetFieldIntegerBoxView($fields['units_size']));

	return (new CWidgetFieldsGroupView(_('Value')))
		->addField(
			new CWidgetFieldIntegerBoxView($fields['decimal_places'])
		)
		->addItem([
			$decimal_size->getLabel(),
			(new CFormField([$decimal_size->getView(), '%']))->addClass('field-size')
		])
		->addItem(
			new CTag('hr')
		)
		->addField(
			new CWidgetFieldRadioButtonListView($fields['value_h_pos'])
		)
		->addItem([
			$value_size->getLabel(),
			(new CFormField([$value_size->getView(), '%']))->addClass('field-size')
		])
		->addField(
			new CWidgetFieldRadioButtonListView($fields['value_v_pos'])
		)
		->addField(
			new CWidgetFieldCheckBoxView($fields['value_bold'])
		)
		->addField(
			(new CWidgetFieldColorView($fields['value_color']))->addLabelClass('offset-3')
		)
		->addItem(
			new CTag('hr')
		)
		->addItem(
			(new CDiv([
				$units_show->getView(),
				$units->getLabel()
			]))->addClass('units-show')
		)
		->addItem(
			(new CFormField(
				$units->getView()
			))->addClass(CFormField::ZBX_STYLE_FORM_FIELD_FLUID)
		)
		->addField(
			(new CWidgetFieldSelectView($fields['units_pos']))
				->setHelpHint(_('Position is ignored for s, uptime and unixtime units.'))
		)
		->addItem([
			$units_size->getLabel(),
			(new CFormField([$units_size->getView(), '%']))->addClass('field-size')
		])
		->addField(
			(new CWidgetFieldCheckBoxView($fields['units_bold']))->addLabelClass('offset-3')
		)
		->addField(
			(new CWidgetFieldColorView($fields['units_color']))->addLabelClass('offset-3')
		)
		->addRowClass('fields-group-value');
}

function getTimeFieldsGroupViews(CWidgetFormView $form, array $fields): CWidgetFieldsGroupView {
	$time_size = $form->registerField(new CWidgetFieldIntegerBoxView($fields['time_size']));

	return (new CWidgetFieldsGroupView(_('Time')))
		->addField(
			new CWidgetFieldRadioButtonListView($fields['time_h_pos'])
		)
		->addItem([
			$time_size->getLabel(),
			(new CFormField([$time_size->getView(), '%']))->addClass('field-size')
		])
		->addField(
			new CWidgetFieldRadioButtonListView($fields['time_v_pos'])
		)
		->addField(
			new CWidgetFieldCheckBoxView($fields['time_bold'])
		)
		->addField(
			(new CWidgetFieldColorView($fields['time_color']))->addLabelClass('offset-3')
		)
		->addRowClass('fields-group-time');
}

function getChangeIndicatorFieldsGroupViews(array $fields): CWidgetFieldsGroupView {
	return (new CWidgetFieldsGroupView(_('Change indicator')))
		->addItem(
			(new CSvgArrow(['up' => true, 'fill_color' => $fields['up_color']->getValue()]))
				->setId('change-indicator-up')
				->setSize(14, 20))
		->addField(
			(new CWidgetFieldColorView($fields['up_color']))->removeLabel()
		)
		->addItem(
			(new CSvgArrow(['down' => true, 'fill_color' => $fields['down_color']->getValue()]))
				->setId('change-indicator-down')
				->setSize(14, 20),
		)
		->addField(
			(new CWidgetFieldColorView($fields['down_color']))->removeLabel()
		)
		->addItem(
			(new CSvgArrow(['up' => true, 'down' => true, 'fill_color' => $fields['updown_color']->getValue()]))
				->setId('change-indicator-updown')
				->setSize(14, 20)
		)
		->addField(
			(new CWidgetFieldColorView($fields['updown_color']))->removeLabel()
		)
		->addRowClass('fields-group-change-indicator');
}
