<?php
/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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


class CControllerHousekeepingUpdate extends CController {

	protected function checkInput() {
		$fields = [
			'hk_trends'				=> 'db config.hk_trends|time_unit 0,'.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_trends_global'		=> 'db config.hk_trends_global | in 1',
			'hk_trends_mode'		=> 'db config.hk_trends_mode',
			'hk_history'			=> 'db config.hk_history|time_unit 0,'.implode(':', [SEC_PER_HOUR, 25 * SEC_PER_YEAR]),
			'hk_history_global'		=> 'db config.hk_history_global | in 1',
			'hk_history_mode'		=> 'db config.hk_history_mode',
			'hk_sessions'			=> 'db config.hk_sessions|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_sessions_mode'		=> 'db config.hk_sessions_mode | in 1',
			'hk_audit'				=> 'db config.hk_audit|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_audit_mode'			=> 'db config.hk_audit_mode | in 1',
			'hk_services'			=> 'db config.hk_services|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_services_mode'		=> 'db config.hk_services_mode | in 1',
			'hk_events_autoreg'		=> 'db config.hk_events_autoreg|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_events_discovery'	=> 'db config.hk_events_discovery|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_events_internal'	=> 'db config.hk_events_internal|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_events_trigger'		=> 'db config.hk_events_trigger|time_unit '.implode(':', [SEC_PER_DAY, 25 * SEC_PER_YEAR]),
			'hk_events_mode'		=> 'db config.hk_events_mode | in 1',
			'compression_status'	=> 'db config.compression_status | in 1',
			'compress_older'		=> 'db config.compress_older|time_unit '.implode(':', [7 * SEC_PER_DAY, 25 * SEC_PER_YEAR])
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			switch ($this->GetValidationError()) {
				case self::VALIDATION_ERROR:
					$response = new CControllerResponseRedirect((new CUrl('zabbix.php'))
						->setArgument('action', 'housekeeping.edit')
						->getUrl()
					);
					$response->setFormData($this->getInputAll() + [
						'hk_events_mode' => '0',
						'hk_services_mode' => '0',
						'hk_audit_mode' => '0',
						'hk_sessions_mode' => '0',
						'hk_history_mode' => '0',
						'hk_history_global' => '0',
						'hk_trends_mode' => '0',
						'hk_trends_global' => '0',
						'compression_status' => '0',
					]);
					$response->setMessageError(_('Cannot update configuration'));
					$this->setResponse($response);
					break;
				case self::VALIDATION_FATAL_ERROR:
					$this->setResponse(new CControllerResponseFatal());
					break;
			}
		}

		return $ret;
	}

	protected function checkPermissions() {
		return ($this->getUserType() == USER_TYPE_SUPER_ADMIN);
	}

	protected function doAction() {
		$hk = [
			CHousekeepingHelper::HK_EVENTS_MODE => $this->getInput('hk_events_mode', 0),
			CHousekeepingHelper::HK_SERVICES_MODE => $this->getInput('hk_services_mode', 0),
			CHousekeepingHelper::HK_AUDIT_MODE => $this->getInput('hk_audit_mode', 0),
			CHousekeepingHelper::HK_SESSIONS_MODE => $this->getInput('hk_sessions_mode', 0),
			CHousekeepingHelper::HK_HISTORY_MODE => $this->getInput('hk_history_mode', 0),
			CHousekeepingHelper::HK_HISTORY_GLOBAL => $this->getInput('hk_history_global', 0),
			CHousekeepingHelper::HK_TRENDS_MODE => $this->getInput('hk_trends_mode', 0),
			CHousekeepingHelper::HK_TRENDS_GLOBAL => $this->getInput('hk_trends_global', 0),
			CHousekeepingHelper::COMPRESSION_STATUS => $this->getInput('compression_status', 0),
			CHousekeepingHelper::COMPRESS_OLDER => $this->getInput('compress_older', DB::getDefault('config',
				'compress_older'
			))
		];

		if ($hk[CHousekeepingHelper::COMPRESSION_STATUS] === 0) {
			unset($hk[CHousekeepingHelper::COMPRESS_OLDER]);
		}

		if ($hk[CHousekeepingHelper::HK_EVENTS_MODE] == 1) {
			$this->getInputs($hk, [CHousekeepingHelper::HK_EVENTS_TRIGGER, CHousekeepingHelper::HK_EVENTS_INTERNAL,
				CHousekeepingHelper::HK_EVENTS_DISCOVERY, CHousekeepingHelper::HK_EVENTS_AUTOREG
			]);
		}

		if ($hk[CHousekeepingHelper::HK_SERVICES_MODE] == 1) {
			$hk[CHousekeepingHelper::HK_SERVICES] = $this->getInput('hk_services');
		}

		if ($hk[CHousekeepingHelper::HK_AUDIT_MODE] == 1) {
			$hk[CHousekeepingHelper::HK_AUDIT] = $this->getInput('hk_audit');
		}

		if ($hk[CHousekeepingHelper::HK_SESSIONS_MODE] == 1) {
			$hk[CHousekeepingHelper::HK_SESSIONS] = $this->getInput('hk_sessions');
		}

		if ($hk[CHousekeepingHelper::HK_HISTORY_GLOBAL] == 1) {
			$hk[CHousekeepingHelper::HK_HISTORY] = $this->getInput('hk_history');
		}

		if ($hk[CHousekeepingHelper::HK_TRENDS_GLOBAL] == 1) {
			$hk[CHousekeepingHelper::HK_TRENDS] = $this->getInput('hk_trends');
		}

		$result = API::Housekeeping()->update($hk);

		$response = new CControllerResponseRedirect((new CUrl('zabbix.php'))
			->setArgument('action', 'housekeeping.edit')
		);

		if ($result) {
			$response->setMessageOk(_('Configuration updated'));
		}
		else {
			$response->setFormData($this->getInputAll());
			$response->setMessageError(_('Cannot update configuration'));
		}

		$this->setResponse($response);
	}
}
