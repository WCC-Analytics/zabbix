<?php
/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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


require_once dirname(__FILE__).'/../../include/CWebTest.php';
require_once dirname(__FILE__).'/../../include/helpers/CDataHelper.php';
require_once dirname(__FILE__).'/../behaviors/CMessageBehavior.php';

/**
 * @backup config, hstgrp, widget
 *
 * @onBefore prepareDashboardData, prepareProblemsData
 */
class testDashboardProblemsWidgetDisplay extends CWebTest {

	use TableTrait;

	private static $dashboardid;
	private static $time;

	/**
	 * Attach MessageBehavior to the test.
	 *
	 * @return array
	 */
	public function getBehaviors() {
		return [CMessageBehavior::class];
	}

	public function prepareDashboardData() {
		$response = CDataHelper::call('dashboard.create', [
			'name' => 'Dashboard for Problem widget check',
			'auto_start' => 0,
			'pages' => [
				[
					'name' => 'First Page',
					'display_period' => 3600
				]
			]
		]);

		$this->assertArrayHasKey('dashboardids', $response);
		self::$dashboardid = $response['dashboardids'][0];
	}

	public function prepareProblemsData() {
		// Create hostgroup for hosts with items triggers.
		$hostgroups = CDataHelper::call('hostgroup.create', [
			['name' => 'Group for Problems Widgets'],
			['name' => 'Group for Cause and Symptoms'],
		]);
		$this->assertArrayHasKey('groupids', $hostgroups);
		$problem_groupid = $hostgroups['groupids'][0];
		$symptoms_groupid = $hostgroups['groupids'][1];

		// Create host for items and triggers.
		$hosts = CDataHelper::call('host.create', [
			[
				'host' => 'Host for Problems Widgets',
				'groups' => [['groupid' => $problem_groupid]]
			],
			[
				'host' => 'Host for Cause and Symptoms',
				'groups' => [['groupid' => $symptoms_groupid]]
			]
		]);
		$this->assertArrayHasKey('hostids', $hosts);
		$problem_hostid = $hosts['hostids'][0];
		$symptoms_hostid = $hosts['hostids'][1];

		// Create items on previously created hosts.
		$problem_item_names = ['float', 'char', 'log', 'unsigned', 'text'];

		$problem_items_data = [];
		foreach ($problem_item_names as $i => $item) {
			$problem_items_data[] = [
				'hostid' => $problem_hostid,
				'name' => $item,
				'key_' => $item,
				'type' => ITEM_TYPE_TRAPPER,
				'value_type' => $i
			];
		}

		$problem_items = CDataHelper::call('item.create', $problem_items_data);
		$this->assertArrayHasKey('itemids', $problem_items);
		$problem_itemids = CDataHelper::getIds('name');

		$symptoms_items_data = [];
		foreach (['trap1', 'trap2', 'trap3'] as $i => $item) {
			$symptoms_items_data[] = [
				'hostid' => $symptoms_hostid,
				'name' => $item,
				'key_' => $item,
				'type' => ITEM_TYPE_TRAPPER,
				'value_type' => $i
			];
		}

		$symptoms_items = CDataHelper::call('item.create', $symptoms_items_data);
		$this->assertArrayHasKey('itemids', $symptoms_items);
		$symptoms_itemids = CDataHelper::getIds('name');

		// Create triggers based on items.
		$problem_triggers = CDataHelper::call('trigger.create', [
			[
				'description' => 'Trigger for widget 1 float',
				'expression' => 'last(/Host for Problems Widgets/float)=0',
				'opdata' => 'Item value: {ITEM.LASTVALUE}',
				'priority' => 0
			],
			[
				'description' => 'Trigger for widget 1 char',
				'expression' => 'last(/Host for Problems Widgets/char)=0',
				'priority' => 1,
				'manual_close' => 1
			],
			[
				'description' => 'Trigger for widget 2 log',
				'expression' => 'last(/Host for Problems Widgets/log)=0',
				'priority' => 2
			],
			[
				'description' => 'Trigger for widget 2 unsigned',
				'expression' => 'last(/Host for Problems Widgets/unsigned)=0',
				'opdata' => 'Item value: {ITEM.LASTVALUE}',
				'priority' => 3
			],
			[
				'description' => 'Trigger for widget text',
				'expression' => 'last(/Host for Problems Widgets/text)=0',
				'priority' => 4
			]
		]);
		$this->assertArrayHasKey('triggerids', $problem_triggers);
		$problem_triggerids = CDataHelper::getIds('description');

		$symptoms_triggers = CDataHelper::call('trigger.create', [
			[
				'description' => 'Cause problem 1',
				'expression' => 'last(/Host for Cause and Symptoms/trap1)=0',
				'priority' => 0
			],
			[
				'description' => 'Symptom problem 2',
				'expression' => 'last(/Host for Cause and Symptoms/trap2)=0',
				'priority' => 1
			],
			[
				'description' => 'Symptom problem 3',
				'expression' => 'last(/Host for Cause and Symptoms/trap3)=0',
				'priority' => 2
			]
		]);
		$this->assertArrayHasKey('triggerids', $symptoms_triggers);
		$symptoms_triggerids = CDataHelper::getIds('description');

		// Create events and problems.
		self::$time = time();

		foreach (array_values($problem_itemids) as $itemid) {
			CDataHelper::addItemData($itemid, 0);
		}

		foreach (array_values($symptoms_itemids) as $itemid) {
			CDataHelper::addItemData($itemid, 0);
		}

		$i = 0;
		foreach ($problem_triggerids as $name => $id) {
			DBexecute('INSERT INTO events (eventid, source, object, objectid, clock, ns, value, name, severity) VALUES ('.
				(1009950 + $i).', 0, 0, '.zbx_dbstr($id).', '.self::$time.', 0, 1, '.zbx_dbstr($name).', '.zbx_dbstr($i).')'
			);
			DBexecute('INSERT INTO problem (eventid, source, object, objectid, clock, ns, name, severity) VALUES ('.
				(1009950 + $i).', 0, 0, '.zbx_dbstr($id).', '.self::$time.', 0, '.zbx_dbstr($name).', '.zbx_dbstr($i).')'
			);
			$i++;
		}

		$j = 0;
		foreach ($symptoms_triggerids as $name => $id) {
			DBexecute('INSERT INTO events (eventid, source, object, objectid, clock, ns, value, name, severity) VALUES ('.
				(1009850 + $j).', 0, 0, '.zbx_dbstr($id).', '.self::$time.', 0, 1, '.zbx_dbstr($name).', '.zbx_dbstr($j).')'
			);
			DBexecute('INSERT INTO problem (eventid, source, object, objectid, clock, ns, name, severity) VALUES ('.
				(1009850 + $j).', 0, 0, '.zbx_dbstr($id).', '.self::$time.', 0, '.zbx_dbstr($name).', '.zbx_dbstr($j).')'
			);
			$j++;
		}

		// Change triggers' state to Problem.
		DBexecute('UPDATE triggers SET value = 1 WHERE description IN ('.zbx_dbstr('Trigger for widget 1 float').', '.
			zbx_dbstr('Trigger for widget 2 log').', '.zbx_dbstr('Trigger for widget 2 unsigned').', '.
			zbx_dbstr('Trigger for widget text').', '.zbx_dbstr('Cause problem 1').', '.
			zbx_dbstr('Symptom problem 2').', '.zbx_dbstr('Symptom problem 3').')'
		);

		// Manual close is true for the problem: Trigger for widget 1 char.
		DBexecute('UPDATE triggers SET value = 1, manual_close = 1 WHERE description = '.
			zbx_dbstr('Trigger for widget 1 char')
		);

		// Set cause and symptoms.
		DBexecute('UPDATE problem SET cause_eventid = 1009850 WHERE name IN ('.zbx_dbstr('Symptom problem 2').', '.
			zbx_dbstr('Symptom problem 3').')'
		);
		DBexecute('INSERT INTO event_symptom (eventid, cause_eventid) VALUES (1009851, 1009850)');
		DBexecute('INSERT INTO event_symptom (eventid, cause_eventid) VALUES (1009852, 1009850)');

		// Suppress the problem: 'Trigger for widget text'.
		DBexecute('INSERT INTO event_suppress (event_suppressid, eventid, maintenanceid, suppress_until) VALUES '.
			'(100990, 1009954, NULL, 0)'
		);

		// Acknowledge the problem: 'Trigger for widget 2 unsigned' and get acknowledge time.
		CDataHelper::call('event.acknowledge', [
			'eventids' => 1009953,
			'action' => 6,
			'message' => 'Acknowledged event'
		]);
	}

	public static function getCheckWidgetTable() {
		return [
			// #0 Filtered by Host group.
			[
				[
					'fields' => [
						'Name' => 'Group filter',
						'Host groups' => 'Group for Problems Widgets'
					],
					'result' => [
						'Trigger for widget 2 unsigned',
						'Trigger for widget 2 log',
						'Trigger for widget 1 char',
						'Trigger for widget 1 float'
					]
				]
			],
			// #1 Filtered by Host group, show suppressed.
			[
				[
					'fields' => [
						'Name' => 'Group, unsupressed filter',
						'Host groups' => 'Group for Problems Widgets',
						'Show suppressed problems' => true
					],
					'result' => [
						'Trigger for widget text',
						'Trigger for widget 2 unsigned',
						'Trigger for widget 2 log',
						'Trigger for widget 1 char',
						'Trigger for widget 1 float'
					]
				]
			],
			// #2 Filtered by Host group, show unacknowledged.
			[
				[
					'fields' => [
						'Name' => 'Group, unucknowledged filter',
						'Host groups' => 'Group for Problems Widgets',
						'Show unacknowledged only' => true
					],
					'result' => [
						'Trigger for widget 2 log',
						'Trigger for widget 1 char',
						'Trigger for widget 1 float'
					]
				]
			],
			// #3 Filtered by Host group, Sort by problem.
			[
				[
					'fields' => [
						'Name' => 'Group, sort by Problem ascending filter',
						'Host groups' => 'Group for Problems Widgets',
						'Sort entries by' => 'Problem (ascending)'
					],
					'result' => [
						'Trigger for widget 1 char',
						'Trigger for widget 1 float',
						'Trigger for widget 2 log',
						'Trigger for widget 2 unsigned'
					],
					'headers' => ['Time', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity', 'Duration',
						'Update', 'Actions'
					]
				]
			],
			// #4 Filtered by Host, Sort by severity.
			[
				[
					'fields' => [
						'Name' => 'Group, sort by Severity ascending filter',
						'Hosts' => 'Host for Problems Widgets',
						'Sort entries by' => 'Severity (ascending)'
					],
					'result' => [
						'Trigger for widget 1 float',
						'Trigger for widget 1 char',
						'Trigger for widget 2 log',
						'Trigger for widget 2 unsigned'
					],
					'headers' => ['Time', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity', 'Duration',
						'Update', 'Actions'
					]
				]
			],
			// #5 Filtered by Host, Problem.
			[
				[
					'fields' => [
						'Name' => 'Group, Problem filter',
						'Hosts' => 'Host for Problems Widgets',
						'Problem' => 'Trigger for widget 2'
					],
					'result' => [
						'Trigger for widget 2 unsigned',
						'Trigger for widget 2 log'
					]
				]
			],
			// #6 Filtered by Excluded groups.
			[
				[
					'fields' => [
						'Name' => 'Group, Excluded groups',
						'Exclude host groups' => [
							'Group for Problems Widgets',
							'Zabbix servers',
							'Group to check triggers filtering',
							'Another group to check Overview',
							'Group to check Overview',
							'Group for Cause and Symptoms'
						]
					],
					'result' => [
						'Trigger for tag permissions Oracle',
						'Trigger for tag permissions MySQL'
					]
				]
			],
			// #7 Filtered by Host, some severities.
			[
				[
					'fields' => [
						'Name' => 'Group, some severities',
						'Hosts' => 'Host for Problems Widgets',
						'id:severities_0' => true,
						'id:severities_2' => true,
						'id:severities_4' => true
					],
					'table_result' => [
						[
							'Recovery time' => '',
							'Status' => 'PROBLEM',
							'Info' => '',
							'Host' => 'Host for Problems Widgets',
							'Problem • Severity' => 'Trigger for widget 2 log',
							'Update' => 'Update'
						],
						[
							'Recovery time' => '',
							'Status' => 'PROBLEM',
							'Info' => '',
							'Host' => 'Host for Problems Widgets',
							'Problem • Severity' => 'Trigger for widget 1 float',
							'Update' => 'Update'
						]
					]
				]
			],
			// #8 Filtered by Host group, tags.
			[
				[
					'fields' => [
						'Name' => 'Group, tags, show 1',
						'Host groups' => 'Zabbix servers',
						'Show tags' => 1
					],
					'Tags' => [
						'tags' => [
							[
								'action' => USER_ACTION_UPDATE,
								'index' => 0,
								'tag' => 'Delta',
								'operator' => 'Exists'
							]
						]
					],
					'result' => [
						'Fourth test trigger with tag priority',
						'First test trigger with tag priority'
					],
					'tags_display' => [
						'Delta: t',
						'Alpha: a'
					],
					'headers' => ['Time', '', '', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity',
						'Duration', 'Update', 'Actions', 'Tags'
					]
				]
			],
			// #9 Filtered by Host group, tag + value.
			[
				[
					'fields' => [
						'Name' => 'Group, tags, show 2',
						'Host groups' => 'Zabbix servers',
						'Show tags' => 2
					],
					'Tags' => [
						'tags' => [
							[
								'action' => USER_ACTION_UPDATE,
								'index' => 0,
								'tag' => 'Eta',
								'operator' => 'Equals',
								'value' => 'e'
							]
						]
					],
					'result' => [
						'Fourth test trigger with tag priority',
						'Second test trigger with tag priority'
					],
					'tags_display' => [
						'Eta: eDelta: t',
						'Eta: eBeta: b'
					],
					'headers' => ['Time', '', '', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity',
						'Duration', 'Update', 'Actions', 'Tags'
					]
				]
			],
			// #10 Filtered by Host group, Operator: Or, show 3, shortened.
			[
				[
					'fields' => [
						'Name' => 'Group, tags, show 3, shortened',
						'Host groups' => 'Zabbix servers',
						'Show tags' => 3,
						'Tag name' => 'Shortened',
						'Show timeline' => false
					],
					'Tags' => [
						'evaluation' => 'Or',
						'tags' => [
							[
								'action' => USER_ACTION_UPDATE,
								'index' => 0,
								'tag' => 'Theta',
								'operator' => 'Contains',
								'value' => 't'
							],
							[
								'tag' => 'Tag4',
								'operator' => 'Exists'
							]
						]
					],
					'result' => [
						'Test trigger to check tag filter on problem page',
						'Fourth test trigger with tag priority',
						'Third test trigger with tag priority'
					],
					'tags_display' => [
						'DatSer: abcser: abcdef',
						'The: tDel: tEta: e',
						'The: tAlp: aIot: i'
					],
					'headers' => ['Time', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity', 'Duration',
						'Update', 'Actions', 'Tags'
					]
				]
			],
			// #11 Filtered by Host group, tags, show 3, shortened, tag priority.
			[
				[
					'fields' => [
						'Name' => 'Group, tags, show 3, shortened, tag priority',
						'Host groups' => 'Zabbix servers',
						'Show tags' => 3,
						'Tag name' => 'None',
						'Show timeline' => false,
						'Tag display priority' => 'Gamma, Eta'
					],
					'Tags' => [
						'evaluation' => 'And/Or',
						'tags' => [
							[
								'action' => USER_ACTION_UPDATE,
								'index' => 0,
								'tag' => 'Theta',
								'operator' => 'Equals',
								'value' => 't'
							],
							[
								'tag' => 'Kappa',
								'operator' => 'Does not exist'
							]
						]
					],
					'result' => [
						'Fourth test trigger with tag priority'
					],
					'tags_display' => ['get'],
					'headers' => ['Time', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity', 'Duration',
						'Update', 'Actions', 'Tags'
					]
				]
			],
			// #12 Filtered by Host, operational data - Separately, Show suppressed.
			[
				[
					'fields' => [
						'Name' => 'Host, operational data - Separately, Show suppressed',
						'Hosts' => 'Host for Problems Widgets',
						'Show operational data' => 'Separately',
						'Show suppressed problems' => true
					],
					'result' => [
						'Trigger for widget text',
						'Trigger for widget 2 unsigned',
						'Trigger for widget 2 log',
						'Trigger for widget 1 char',
						'Trigger for widget 1 float'
					],
					'operational_data' => [
						'0',
						"Item value: \n0",
						'0',
						'0',
						"Item value: \n0"
					],
					'headers' => ['Time', '', '', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity',
						'Operational data', 'Duration', 'Update', 'Actions'
					]
				]
			],
			// #13 Filtered by Host, operational data - With problem name, Show unacknowledged.
			[
				[
					'fields' => [
						'Name' => 'Host, operational data - With problem name, Show unacknowledged',
						'Hosts' => 'Host for Problems Widgets',
						'Show operational data' => 'With problem name',
						'Show unacknowledged only' => true
					],
					'result' => [
						'Trigger for widget 2 log',
						'Trigger for widget 1 char',
						"Trigger for widget 1 float (Item value: \n0)"
					]
				]
			],
			// #14 Filtered by Host group, show lines = 2.
			[
				[
					'fields' => [
						'Name' => 'Host group, show lines = 2',
						'Host groups' => 'Group for Problems Widgets',
						'Show lines' => 2
					],
					'result' => [
						'Trigger for widget 2 unsigned',
						'Trigger for widget 2 log'
					],
					'stats' => '2 of 4 problems are shown'
				]
			],
			// #15 Filtered by Host group, show symptoms = false.
			[
				[
					'fields' => [
						'Name' => 'Host group, show symptoms = false',
						'Host groups' => 'Group for Cause and Symptoms',
						'Show symptoms' => false
					],
					'result' => [
						'Cause problem 1',
						'Symptom problem 3',
						'Symptom problem 2'
					],
					'headers' => ['', '', 'Time', '', '', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity',
						'Duration', 'Update', 'Actions'
					]
				]
			],
			// #16 Filtered by Host group, show symptoms = true.
			[
				[
					'fields' => [
						'Name' => 'Host group, show symptoms = true',
						'Host groups' => 'Group for Cause and Symptoms',
						'Show symptoms' => true
					],
					'result' => [
						'Symptom problem 3',
						'Symptom problem 2',
						'Cause problem 1',
						'Symptom problem 3',
						'Symptom problem 2'
					],
					'headers' => ['', '', 'Time', '', '', 'Recovery time', 'Status', 'Info', 'Host', 'Problem • Severity',
						'Duration', 'Update', 'Actions'
					]
				]
			]
		];
	}

	/**
	 * @dataProvider getCheckWidgetTable
	 */
	public function testDashboardProblemsWidgetDisplay_CheckTable($data) {
		$this->page->login()->open('zabbix.php?action=dashboard.view&dashboardid='.self::$dashboardid);
		$dashboard = CDashboardElement::find()->one();
		$form = $dashboard->edit()->addWidget()->asForm();
		$dialog = COverlayDialogElement::find()->one()->waitUntilReady();

		// Fill Problems widget filter.
		$form->fill(['Type' => CFormElement::RELOADABLE_FILL('Problems')]);
		$form->fill($data['fields']);

		if (array_key_exists('Tags', $data)) {
			$form->getField('id:evaltype')->fill(CTestArrayHelper::get($data['Tags'], 'evaluation', 'And/Or'));
			$form->getField('id:tags_table_tags')->asMultifieldTable()->fill($data['Tags']['tags']);
		}

		$form->submit();

		// Check saved dashboard.
		$dialog->ensureNotPresent();
		$dashboard->save();
		$this->assertMessage(TEST_GOOD, 'Dashboard updated');

		// Assert Problems widget's table.
		$dashboard->getWidget($data['fields']['Name'])->waitUntilReady();
		$table = $this->query('class:list-table')->asTable()->one();

		// For easier maintenance we'll check the whole table only in some cases.
		if (CTestArrayHelper::get($data, 'table_result')) {
			foreach ($data['table_result'] as &$row) {
				$row['Time'] = date('Y-m-d H:i:s', self::$time);
			}

			$this->assertTableHasData($data['table_result']);
		}
		else {
			// Assert table headers depending on widget settings.
			$headers = (CTestArrayHelper::get($data, 'headers', ['Time', '', '', 'Recovery time', 'Status', 'Info',
					'Host', 'Problem • Severity', 'Duration', 'Update', 'Actions']
			));
			$this->assertEquals($headers, $table->getHeadersText());

			// When there are shown less lines than filtered, table appears unusual and doesn't fit for framework functions.
			if (CTestArrayHelper::get($data['fields'], 'Show lines')) {
				$this->assertEquals(count($data['result']) + 1, $table->getRows()->count());

				// Assert table rows.
				$result = [];
				for ($i = 0; $i < count($data['result']); $i++) {
					$result[] = $table->getRow($i)->getColumn('Problem • Severity')->getText();
				}

				$this->assertEquals($data['result'], $result);

				// Assert table stats.
				$this->assertEquals($data['stats'], $table->getRow(count($data['result']))->getText());
			}
			else {
				$this->assertTableDataColumn($data['result'], 'Problem • Severity');
			}

			if (CTestArrayHelper::get($data, 'operational_data')) {
				$this->assertTableDataColumn($data['operational_data'], 'Operational data');
			}

			// Assert Problems widget's tags column.
			if (array_key_exists('Tags', $data)) {
				$this->assertTableDataColumn($data['tags_display'], 'Tags');
			}
		}

		// Delete created widget.
		DBexecute('DELETE FROM widget'.
			' WHERE dashboard_pageid'.
			' IN (SELECT dashboard_pageid'.
			' FROM dashboard_page'.
			' WHERE dashboardid='.self::$dashboardid.
			')'
		);
	}
}
